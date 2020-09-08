//
//	this code is completly unsafe
//
#include <cstdio>
#include <cstring>

struct SScriptCommand
{
	bool bValid;
	char cCommandName[64];
	char cArguments[32];

	SScriptCommand()
	{
		bValid = false;
		cCommandName[0] = 0;
		memset(cArguments, 0, sizeof(cArguments));
	}
};

inline int str2cmd(const char* str)
{
	int i = 0;
	sscanf(str, "%X", &i);
	i &= 0x7FFF;
	return i;
}

inline bool sf(char* str)
{
	char* p;

	p = strchr(str, '\r');
	if(p) *p = 0;
	p = strchr(str, '\n');
	if(p) *p = 0;

	return(*str != ';' && *str != 0);
}


int main(int argv, char* argc[])
{
	char buffer[1024], *key, *value;
	FILE *keywords, *scm, *out;
	SScriptCommand* commands;
	int cmd;
	int highestCommand = -1;
	char c;

	commands = new SScriptCommand[0x7FFF];
	keywords = fopen("keywords.txt", "rt");
	scm = fopen("SCM.ini", "rt");
	out = fopen("out.ini", "wt");

	if(!keywords || !scm || !out)
		return puts("FAIL #1"), 1;

	while(fgets(buffer, sizeof(buffer), keywords))
	{
		if(sf(buffer) && (value = strchr(buffer, '=')))
		{
			key = buffer;
			*value++ = 0;

			cmd = str2cmd(key);
			if(cmd > highestCommand) highestCommand = cmd;
			strcpy(commands[cmd].cCommandName, value);
			commands[cmd].bValid = true;
		}
	}


	// params must be in original order
	while(fgets(buffer, sizeof(buffer), scm))
	{
		if(sf(buffer) && (value = strchr(buffer, '=')))
		{
			key = buffer;
			*value++ = 0;

			cmd = str2cmd(key);
			if(cmd > highestCommand) highestCommand = cmd;
			char* params = commands[cmd].cArguments;

			for(char* p = value; *p; )
			{
				if(*p++ == '%')
				{
					while(*p >= '0' && *p <= '9') ++p;
					c = *p++;
					++p;

					if(c == 'l') *params++ = 'l';
					else if(c == 'o') *params++ = 'm';
					else *params++ = 'a';
				}
			}
		}
	}

	printf("Highest command: %d\n", highestCommand);
	for(int i = 0; i <= highestCommand; ++i)
	{
		if(commands[i].bValid)
		{
			bool a = true;
			if(commands[i].cArguments[0])
			{
				for(char* p = commands[i].cArguments; *p; ++p)
					if(*p != 'a') { a = false; break; }
			}

			fprintf(out, "%.4X=%s,%s\n", i, commands[i].cCommandName, a? "*" : commands[i].cArguments);
		}
	}

	
	fclose(keywords);
	fclose(scm);
	fclose(out);
	puts("Done");
	return 0;
}

