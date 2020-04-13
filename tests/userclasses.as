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

    void use_field()
    {
        member = 1234;
        print(member);
    }

    int member;
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

void method_field_test()
{
    Foo f;
    f.use_field();
}
