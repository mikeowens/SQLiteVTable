
void rtrim(char *string)
{
	char *cp = strchr(string, 0) - 1;

	while (cp >= (char *) string && (isspace(*cp)))
    {
		*cp-- = 0;
    }
}
