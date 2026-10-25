# `noncopyable` 类设计文档

## 1. 设计目的

`xmuduo::noncopyable` 是一个只用于继承的基类，用来禁止派生类通过编译器生成的默认操作进行拷贝构造和拷贝赋值。它本身不管理资源，也不提供运行时行为；它把“这个类型不能复制”的约束集中表达在一个可复用的基类中。

典型使用场景是对象具有唯一身份，或多个对象复制后会错误地共同拥有同一资源，例如事件循环、互斥量封装、线程对象、文件描述符或 socket 句柄的所有者。

## 2. 当前实现

```cpp
class noncopyable {
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
```

### 删除拷贝操作

拷贝构造函数和拷贝赋值运算符被显式标记为 `= delete`。当编译器为派生类生成拷贝操作时，它必须复制基类子对象；基类的相应操作不可用，派生类默认生成的对应操作也就不可用。

```cpp
class EventLoop : private xmuduo::noncopyable {
public:
    EventLoop() = default;
};

EventLoop first;
// EventLoop second(first); // 编译错误：基类不可拷贝
// second = first;          // 编译错误：基类不可拷贝赋值
```

这是编译期约束，不需要运行时检查或额外存储空间。

### 构造和析构为 `protected`

`noncopyable()` 必须允许派生类调用，否则派生对象无法构造其基类子对象。它不能公开给普通调用者使用，因此放在 `protected` 区域。

省略这条构造函数并不会达到同样效果：由于已经声明了拷贝构造函数（即使它被删除也属于已声明的构造函数），编译器不会隐式生成默认构造函数。派生类构造时将找不到可用的基类默认构造函数。

析构函数设为 `protected`，使派生类可以正常销毁基类子对象，同时阻止外部通过 `noncopyable*` 直接 `delete`。这个类型不用于多态删除，因此析构函数不需要是 `virtual`。若省略析构函数，编译器会隐式声明一个公开析构函数，失去这项访问限制。

## 3. 继承方式

推荐派生类使用 `private` 继承：

```cpp
class Logger : private noncopyable {
    // ...
};
```

这里的继承不是表达“Logger 是一种 noncopyable”，而是实现内部约束。私有继承将基类的公开和受保护接口隐藏在派生类内部。对使用 `class` 声明的类，未指定继承权限时默认也是 `private`，但显式写出更清晰。

## 4. 移动语义

当前基类没有定义移动构造或移动赋值。声明了拷贝操作后，编译器不会为基类隐式生成移动操作；基类拷贝操作又被删除。因此，通过该基类获得约束的派生类默认也不能移动。

对于地址或身份必须稳定的对象（例如 Logger、事件循环），不可移动通常是合理的。对于独占资源包装类，如果确实需要转移资源所有权，应单独设计正确的移动操作，并确保源对象进入有效状态；不要仅为了让类型可移动就直接为基类默认生成移动操作。

## 5. 约束范围与注意事项

该基类会使编译器生成的派生类拷贝操作不可用，但不能阻止派生类作者显式编写自己的拷贝构造或赋值实现。若某个派生类确实手动实现拷贝，作者可以在自定义操作中默认构造基类部分并复制成员；因此 `noncopyable` 是供项目成员遵循的设计约束，不是阻止所有手写复制代码的语言级封锁。

如果只有一个类型不可复制，在该类型中直接删除拷贝函数通常更直观：

```cpp
class EventLoop {
public:
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
};
```

如果多个类型采用相同策略，`noncopyable` 基类可以减少重复声明。选择基类时也要留意它会影响派生类的移动能力。

## 6. 头文件

实现位于 [`include/noncopyable.h`](../include/noncopyable.h)。该类没有源文件，也没有运行时依赖。
