int fib(int n)
{
	if (n < 2)
	{
		return n;
	}

	return fib(n-1) + fib(n-2);
}

void printfib10()
{
	print(fib(10));
}
