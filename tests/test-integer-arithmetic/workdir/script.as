int8 add_i8(int8 a, int8 b)
{
	return a + b;
}

int16 add_i16(int16 a, int16 b)
{
	return a + b;
}

int add_i32(int a, int b)
{
	return a + b;
}

int64 add_i64(int64 a, int64 b)
{
	return a + b;
}

uint8 add_u8(uint8 a, uint8 b)
{
	return a + b;
}

uint16 add_u16(uint16 a, uint16 b)
{
	return a + b;
}

uint add_u32(uint a, uint b)
{
	return a + b;
}

uint64 add_u64(uint64 a, uint64 b)
{
	return a + b;
}

void signed_add_tests()
{
	print(add_i8(-2, 1));
	print(add_i16(-2, 1));
	print(add_i32(-2, 1));
	print(add_i64(-2, 1));
}

void unsigned_add_tests()
{
	print(add_u8(uint8(-2), 1));
	print(add_u16(uint16(-2), 1));
	print(add_u32(1000000, 2000000));
	print(add_u64(1000000000000, 2000000000000));
}