namespace MyEnum
{
enum MyEnum
{
    A = 10,
    B,
    C
};
}

void main()
{
    MyEnum::MyEnum e = MyEnum::B;
    print(e);
}
