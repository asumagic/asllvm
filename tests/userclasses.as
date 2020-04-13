class Foo
{
    Foo()
    {
        print("hello");
    }

    void foo(int a, int b, int c)
    {
        print(a);
        print(b);
        print(c);
    }
};

void test()
{
    Foo f;
}

void method_test()
{
    Foo f;
    f.foo(123, 456, 789);
}
