int fib(int n)
{
	if (n < 2)
	{
		return n;
	}

	return fib(n-1) + fib(n-2);
}

void foo()
{
	for (int i = 0; i < 10; ++i)
	{
		print(i);
	}
}

string add_string(const string &in a, const string &in b)
{
	return a + b;
}

int global = 0;
void globals(int a)
{
	global = a;
	print(global);
	int v = global;
	print(v);
}

void main()
{

	print(fib(10));
	foo();
	print("hello");

	string s = "hello, ";
	string s2 = "world";
	s += s2;
	print(s);

	print(add_string("hello", ", world"));

	string yolo;
	for (int i = 0; i < 3; ++i)
	{
		yolo = yolo + "yolo";
	}

	print(yolo);

	globals(123);
}
/*
void no_op() {}

void for_loop()
{
  for (int i = 0; i < 10; ++i)
  {
  	no_op();
  }
}

void simple_if(int a, int b)
{
	if (a > b)
	{
		no_op();
	}
}

int global_variable = 123;

int return_global()
{
	return global_variable;
}

void taking_one_parameter(int) {}

void taking_many_parameters(int a, int b, int c, int d)
{
	taking_one_parameter(a);
	taking_one_parameter(b);
	taking_one_parameter(c);
	taking_one_parameter(d);
}

void calling_many_parameters()
{
	taking_many_parameters(1, 2, 3, 4);
}

class C
{
	C() {}

	void foo() {}
}

void using_class()
{
	C c;
	c.foo();
}

void local_variables(int param_a, int param_b, int param_c)
{
	int a, b, c;

	a = 1;
	b = 2;
	c = 3;

	param_a = 1;
	param_b = 2;
	param_c = 3;
}

void different_size_locals()
{
	int8 a;
	int16 b;
	int32 c;
	int64 d;

	a = 1;
	b = 2;
	c = 3;
	d = 4;
}

void string_usage()
{
	string s;
	s += 123;
}*/

/*

int float_to_int_conversion(float x)
{
	return int(x);
}

C@ handle_manip()
{
	C@ c = C();
	return @c;
}

void handle_manip2()
{
	C@ c;
	C@ c2;

	@c = @c2;
}*/