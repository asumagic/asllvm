# Improving performance for generated code

## Use `final` whenever possible

asllvm is able to statically dispatch virtual function calls in a specific scenario: Either the original method
declaration either the class where the original method declaration resides must be `final`.

This is an important optimization for code dealing with a lot of classes. A virtual function lookup is somewhat
expensive and disallows some optimizations (such as inlining).

For example, devirtualization will work in the following scenario:

```angelscript
final class A
{
    void foo() { print("hi"); }
}

void main()
{
    A().foo();
}
```

but not in the following one:

```angelscript
class A
{
    void foo() { print("hi"); }
}

class B : A
{
    final void foo() { print("hello"); }
}

void main()
{
    B().foo();
}
```

(Note, devirtualization might be extended to support more cases in the future.)
