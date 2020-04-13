class Foo
{
    Foo()
    {
        print("hello");
    }

    void foo()
    {
        print("world");
    }
};

void test()
{
    Foo f;
}

void method_test()
{
    Foo f;
    f.foo();
}
