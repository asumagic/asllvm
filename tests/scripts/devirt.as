class A
{
    void foo() final
    {
        print("hello");
    }
};

class B : A {};

void main()
{
    B().foo();
}
