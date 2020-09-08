#include <cstdio>

int main()
{
	char buffer[512];

	FILE* s = fopen("ordered_commands.txt", "rt");
	FILE* out = fopen("out.txt", "wt");
	if(!s || !out) return 0;

	for(int cmd = 0; fgets(buffer, sizeof(buffer), s); ++cmd)
	{
		fprintf(out, "%.4X=%s", cmd, buffer);
	}

	fclose(s);
	fclose(out);
}
