#include <main.h>

struct img_header_t {
	uint32_t magic;
	uint32_t origin_size;
	uint32_t decompress_size;
	uint32_t crc;
};

static void usage(void)
{
	printf("usage:\r\n");
	printf("    mkimg [-s size] [-k key] <path>\r\n");
}

int main(int argc, char * argv[])
{
	usage();
	return 0;
}

