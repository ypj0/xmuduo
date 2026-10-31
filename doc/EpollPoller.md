# EPollPoller

## 整体结构

```text
EPollPoller
├── epollFd_        // epoll 实例 fd（RAII，析构 close）
├── events_         // epoll_wait 的接收缓冲区（vector，可复用/扩容）
└── channels_       // fd -> Channel* 映射，记录"谁注册了"
```



## `epoll_event`

`epoll_event`用于描述文件描述符上的事件和关联数据，定义在`<sys/epoll.h>`中。

```c
struct epoll_event {
    uint32_t     events;    // 事件掩码（位图）
    epoll_data_t data;      // 用户数据（联合体）
};

typedef union epoll_data {
    void    *ptr;    // 自定义指针（常用，可携带上下文）
    int      fd;     // 关联的文件描述符（常用）
    uint32_t u32;    // 32位整数
    uint64_t u64;    // 64位整数
} epoll_data_t;

```

**核心字段：**

1. `events`(事件标志位)
   - `EPOLLIN` : fd可读(有数据到达)
   - `EPOLLOUT` : fd可写(可发送数据)
   - `EPOLLERR` : fd发生错误
   - `EPOLLHUP` : 启动边缘触发模式(默认水平触发)
   - `EPOLLRDHUP` : 流套接字对端关闭连接(或半关闭)
2.  `data` (用户数据联合体)
   - 常用`fd`或`ptr`存储事件相关的自定义数据(如连接上下文)

**调用逻辑：**

1. 事件注册：通过`epoll_ctl()`添加fd时，初始化`epoll_event`结构

```c
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
// epfd : epoll对象的文件描述符
// op   : 增删改
// fd   : 监听的目标文件描述符
// event: 指向epoll_event的指针，描述要监听的fd的事件，以及关联的用户数据
```

2. 事件处理
   - `epoll_wait()`返回就绪的`epoll_event`数组
   - 通过`event[i].data.ptr`取出自定义指针，可以快速定位哪个连接发生了事件
   - 通过`events[i].events`判断具体的事件类型

```c
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
// epfd       : epoll实例文件描述符
// events     : 内核将就绪事件信息填充到这个数组中
// maxevents  : events数组的大小，即本次调用最多返回的就绪事件
// timeout    : 超时时间
```

> 所以我们给epoll_ctl传入的epoll_event中的events字段是希望监听的事件，而epoll_wait实际返回的是实际监听到的事件，还有epoll_event中的data字段是原样返回的，目的是定位哪个fd的事件？还有为什么返回的是一个epoll_event数组呢？意思是我们可能希望监听的是一组fd的相关事件吗？这些epoll_event数组是以链表方式组织的吗？

#### 1. `events` 字段：请求 vs 实际返回

- **`epoll_ctl` 传入时**：`events` 是**你希望监听的事件掩码**（如 `EPOLLIN`、`EPOLLOUT`、`EPOLLET` 等），相当于"订阅"。
- **`epoll_wait` 返回时**：`events` 是**内核实际检测到的就绪事件**，是"通知"。

#### 2. `data` 字段：原样返回，用于定位

`data` 是一个 `union`，内核**完全不解释它的内容**，只是原样保存并在事件就绪时返回。用 `ptr` 的好处是能把 fd 和连接状态（缓冲区、协议状态机等）打包在一起，避免每次都用 `fd` 去哈希表里查。

#### 3. 为什么返回的是数组？—— 是的，因为监听一组 fd

这正是 epoll 的设计目的：**一个 epoll 实例可以同时监控成千上万个 fd**，一次 `epoll_wait` 调用会把**当前这一批就绪的 fd** 一次性返回，避免像 `select`/`poll` 那样每次都要遍历全部 fd。

所以返回数组的含义是：**"这次等待期间，有这么多个 fd 就绪了，请逐个处理"**。数组大小 `maxevents` 由你决定，通常设为能一次处理完的批量上限（比如 1024）。如果就绪数超过 `maxevents`，剩下的会在下次调用时继续返回。

#### 4. 数组是链表组织吗？

**不是链表**。`epoll_wait` 填充的是你传入的**连续内存数组**（`struct epoll_event events[MAX_EVENTS]`），内核按顺序把就绪事件写进这个数组的前 `nfds` 个位置，返回 `nfds` 表示有效元素个数。你只需 `for (i = 0; i < nfds; i++)` 顺序遍历即可。

内部实现上，内核用的是**就绪链表（ready list）+ 回调机制**：当某个 fd 事件就绪时，内核把它挂到 epoll 实例的就绪链表上；`epoll_wait` 时再从就绪链表取出，**拷贝**到你提供的数组里。也就是说：

- **内核内部**：就绪事件用链表（`rdllist`）管理，效率高，增删 O(1)。
- **返回给用户**：拷贝成连续数组，方便你顺序遍历。

> 增删改事件为什么要额外增加一个channels_的哈希表呢？epoll的底层已经有红黑树了，可以持久化保存，并且增删改的效率已经很高了？

**内核红黑树存的是fd → 事件掩码，而 `channels_` 存的是fd → Channel，两者解决的不是同一个问题。** 这个 map 存在的价值不在效率，而在**语义、状态判断和用户态上下文**。

#### 1. 内核红黑树对用户态是"黑盒"，你查不到它的状态

**用户态没有任何接口能查询它**

`epoll_ctl` 只有 ADD / MOD / DEL 三个**写操作**，没有GET。你想知道当前注册状态，只能自己记录。

而 `updateChannel` 恰恰需要这个信息来决定该调 ADD 还是 MOD，内核红黑树是**持久化存储**，但不是**可查询的 API**。`channels_` 是用户态为了"能判断状态"而必须自己维护的镜像账本。

| 路径                              | 频率           | 用到的结构                                    |
| :-------------------------------- | :------------- | :-------------------------------------------- |
| `epoll_wait` 返回后处理事件       | **高频热路径** | 靠 `data.ptr` 直接拿 `Channel*`，**不查 map** |
| `updateChannel`（读写状态变化时） | 低频           | 查 `channels_` 决定 ADD/MOD/DEL               |
