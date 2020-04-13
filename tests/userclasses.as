class Foo
{
    // Make sure the constructeur is called correctly, and that stuff is correctly executed inside
    Foo()
    {
        print("hello");
    }

    // Test virtual function calls and parameters inside of methods
    void foo(int a, int b, int c)
    {
        print(a);
        print(b);
        print(c);
    }

    // Somewhat random field organization and writes to test for potential writes overlapping with other members.
    void use_field()
    {
        m2 = 20;
        m3 = 30;
        m5 = 50;
        m7 = 70;
        m6 = 60;
        m8 = 80;
        m10 = 100;
        m9 = 90;
        m4 = 40;
        m1 = 10;

        print(m1);
        print(m2);
        print(m3);
        print(m4);
        print(m5);
        print(m6);
        print(m7);
        print(m8);
        print(m9);
        print(m10);
    }

    int8 m1, m2;
    int16 m3;
    int32 m4;
    int64 m5;
    int16 m6;
    int8 m7;
    int16 m8;
    int8 m9;
    int64 m10;
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
