BEGIN \
{
  printf("struct { const gchar *language; const gchar *group; RegExTypes type; const gchar *regex; } REGEX_BUILTIN[] =\n");
  printf("{\n");
}

# comment, empty lines
/^\S*#/ \
{
  next;
}
/^\S*$/ \
{
  next;
}

# error
match($0, /^(\S+)\s+(\S+)\s+error\s+(.*)/, tokens) \
{
  language = tokens[1];
  group    = tokens[2];
  regex    = tokens[3];
  if (language == "*") language = "";
  if (group    == "*") group = "";
  gsub("\\\\","\\\\\\\\",regex);
  printf("  { \"%s\", \"%s\", REGEX_TYPE_ERROR, \"%s\" },\n", language, group, regex);
  next;
}

# warning
match($0, /^(\S+)\s+(\S+)\s+warning\s+(.*)/, tokens) \
{
  language = tokens[1];
  group    = tokens[2];
  regex    = tokens[3];
  if (language == "*") language = "";
  if (group    == "*") group = "";
  gsub("\\\\","\\\\\\\\",regex);
  printf("  { \"%s\", \"%s\", REGEX_TYPE_WARNING, \"%s\" },\n", language, group, regex);
  next;
}

# extension
match($0, /^(\S+)\s+(\S+)\s+extension\s+(.*)/, tokens) \
{
  language = tokens[1];
  group    = tokens[2];
  regex    = tokens[3];
  if (language == "*") language = "";
  if (group    == "*") group = "";
  gsub("\\\\","\\\\\\\\",regex);
  printf("  { \"%s\", \"%s\", REGEX_TYPE_EXTENSION, \"%s\" },\n", language, group, regex);
  next;
}

# enter
match($0, /^(\S+)\s+(\S+)\s+enter\s+(.*)/, tokens) \
{
  language = tokens[1];
  group    = tokens[2];
  regex    = tokens[3];
  if (language == "*") language = "";
  if (group    == "*") group = "";
  gsub("\\\\","\\\\\\\\",regex);
  printf("  { \"%s\", \"%s\", REGEX_TYPE_ENTER, \"%s\" },\n", language, group, regex);
  next;
}

# leave
match($0, /^(\S+)\s+(\S+)\s+leave\s+(.*)/, tokens) \
{
  language = tokens[1];
  group    = tokens[2];
  regex    = tokens[3];
  if (language == "*") language = "";
  if (group    == "*") group = "";
  gsub("\\\\","\\\\\\\\",regex);
  printf("  { \"%s\", \"%s\", REGEX_TYPE_LEAVE, \"%s\" },\n", language, group, regex);
  next;
}

# default
{ \
  printf("ERROR: unknown type '%s' at line %d\n" , $3, FNR) > "/dev/stderr";
  exit(1);
}

END \
{
  printf("};\n");
}
