void main()
{
    print('' + calc(10, 100));
}

int calc(int a, int b)
{
    a *= 10;
    b *= a;
    return b;
}
