#include "cli.h"
#include <string.h>

static const char *const app_demo_lines[] = {
  "{\n",
  "  \"title\": \"charts --demo\",\n",
  "  \"footer\": \"charts --demo   -   n notes   ? keys   q quit\",\n",
  "  \"slides\": [\n",
  "    {\"title\": \"charts\", \"subtitle\": \"DOS-style chart decks for the Linux console\\n\\nleft and right to page  -  n for the speaker's notes  -  ? for every key  -  q to quit\",\n",
  "     \"notes\": \"This deck is built into the binary: charts --demo. Everything in it is a JSON file an agent could have written.\"},\n",
  "    {\"title\": \"December shipped three times as many cabinets as January\",\n",
  "     \"blocks\": [\n",
  "       {\"cols\": [{\"stat\": \"4,639\", \"label\": \"cabinets shipped\", \"delta\": \"+61% on 1990\"},\n",
  "                 {\"stat\": \"640\", \"label\": \"December alone\", \"delta\": \"+205% since Jan\"},\n",
  "                 {\"stat\": \"4.4%\", \"label\": \"returned\", \"delta\": \"-1.1 pt\"}], \"at\": [0, 0, 12, 3]},\n",
  "       {\"type\": \"bar\", \"depth\": 3, \"ylabel\": \"cabinets\", \"colors\": {\"Dec\": \"white\"}, \"at\": [0, 3, 8, 9],\n",
  "        \"annotations\": [{\"at\": \"Dec\", \"text\": \"best month ever\"}],\n",
  "        \"data\": {\"labels\": [\"Jan\",\"Feb\",\"Mar\",\"Apr\",\"May\",\"Jun\",\"Jul\",\"Aug\",\"Sep\",\"Oct\",\"Nov\",\"Dec\"],\n",
  "                 \"series\": [{\"name\": \"shipped\", \"values\": [210,238,225,301,344,322,398,441,420,512,588,640]}]}},\n",
  "       {\"text\": [\"# The year\", \"\", \"- Every quarter beat the last\", \"- **Laser Llama** carried the spring\", \"- June wobbled\",\n",
  "                 \"\", \"> A KPI strip, a chart, words beside it: one slide, one idea.\"], \"at\": [8, 3, 4, 9]}],\n",
  "     \"notes\": \"Stat tiles colour their deltas. One pinned colour (white) points at December.\"},\n",
  "    {\"title\": \"Doubling the price lifted every title but one\",\n",
  "     \"type\": \"dumbbell\", \"values\": true, \"xlabel\": \"dollars per cabinet per day\",\n",
  "     \"colors\": {\"at 25c\": \"grey\", \"at 50c\": \"yellow\"},\n",
  "     \"annotations\": [{\"at\": \"Sad Clam\", \"series\": \"at 50c\", \"text\": \"the only one to fall\", \"color\": \"red\"}],\n",
  "     \"data\": {\"labels\": [\"Laser Llama\", \"Turbo Tapir\", \"Mega Marmot\", \"Pixel Pigeon\", \"Sad Clam\"],\n",
  "              \"series\": [{\"name\": \"at 25c\", \"values\": [103, 80, 72, 48, 18]}, {\"name\": \"at 50c\", \"values\": [151, 112, 96, 61, 11]}]},\n",
  "     \"notes\": \"Before and after: the arrow points at the last series.\"},\n",
  "    {\"title\": \"Uptime never fell below 97%, even in June\",\n",
  "     \"type\": \"bar\", \"min\": 95, \"max\": 100, \"values\": true, \"depth\": 1, \"colors\": {\"Jun\": \"red\"},\n",
  "     \"annotations\": [{\"y\": 97, \"text\": \"contract floor\", \"color\": \"green\"}],\n",
  "     \"data\": {\"labels\": [\"Jan\",\"Feb\",\"Mar\",\"Apr\",\"May\",\"Jun\",\"Jul\",\"Aug\",\"Sep\",\"Oct\",\"Nov\",\"Dec\"],\n",
  "              \"series\": [{\"name\": \"uptime %\", \"values\": [99.1,99.3,98.9,99.2,99.0,97.2,98.8,99.1,99.4,99.2,98.9,98.7]}]},\n",
  "     \"notes\": \"The axis starts at 95, so the bars are drawn torn: nobody reads June as a third of January.\"},\n",
  "    {\"title\": \"How a deck gets made\",\n",
  "     \"flow\": [\"your data\", {\"id\": \"agent\", \"text\": \"an agent\\nfinds the story\"}, \"deck.json\",\n",
  "              {\"id\": \"check\", \"text\": \"charts --check\\n+ PNGs it reads\"}, {\"id\": \"you\", \"text\": \"you, paging\\nthrough it\", \"color\": \"green\"}],\n",
  "     \"labels\": {\"your data>agent\": \"CSV\", \"check>you\": \"launcher\"},\n",
  "     \"edges\": [[\"your data\", \"agent\"], [\"agent\", \"deck.json\"], [\"deck.json\", \"check\"], [\"check\", \"you\"], [\"check\", \"agent\", \"fix it\"]],\n",
  "     \"notes\": \"The human never runs charts by hand: the agent hands over a launcher. This diagram is a flow block: the steps and arrows are named, the layout is done for you.\"},\n",
  "    {\"title\": \"Now yours\",\n",
  "     \"text\": [\"- **charts --example deck** > deck.json\", \"- **charts deck.json --check**\", \"- **charts -h**: the manual, written for agents\",\n",
  "              \"- or install the skill: **make install-skill**\"], \"size\": 2, \"valign\": \"middle\"}\n",
  "  ]\n",
  "}\n",
  NULL
};
static const char *const app_deck_lines[] = {
  "{\n",
  "  \"title\": \"Q3 review\",\n",
  "  \"theme\": \"dos\",\n",
  "  \"footer\": \"ACME  -  Q3 review\",\n",
  "  \"slides\": [\n",
  "    {\"title\": \"Q3 review\", \"subtitle\": \"Revenue, costs and what comes next\"},\n",
  "    {\n",
  "      \"title\": \"Revenue grew every month but June\",\n",
  "      \"type\": \"bar\", \"values\": true, \"ylabel\": \"USD k\",\n",
  "      \"data\": \"revenue.csv\",\n",
  "      \"annotations\": [\n",
  "        {\"at\": \"Apr\", \"series\": \"revenue\", \"text\": \"spring launch\"},\n",
  "        {\"y\": 150, \"text\": \"target\"}\n",
  "      ],\n",
  "      \"notes\": \"June dipped because of the warehouse move.\"\n",
  "    },\n",
  "    {\n",
  "      \"title\": \"Where the money went\",\n",
  "      \"layout\": \"cols\",\n",
  "      \"blocks\": [\n",
  "        {\"type\": \"pie3d\", \"explode\": 0, \"weight\": 2,\n",
  "         \"data\": {\"labels\": [\"payroll\", \"cloud\", \"rent\", \"other\"], \"series\": [{\"name\": \"USD k\", \"values\": [620, 310, 140, 95]}]}},\n",
  "        {\"rows\": [\n",
  "          {\"stat\": \"1,165\", \"label\": \"total spend, USD k\", \"delta\": \"+4% vs Q2\"},\n",
  "          {\"bullets\": [\"Payroll is **53%** of spend\", \"Cloud doubled since Q1\", \"Rent is fixed until 2027\"]}\n",
  "        ]}\n",
  "      ]\n",
  "    }\n",
  "  ]\n",
  "}\n",
  NULL
};
static const char *const app_json_lines[] = {
  "{\n",
  "  \"chart\": {\"type\": \"line\", \"title\": \"Load average\", \"ylabel\": \"load\"},\n",
  "  \"rows\": [\n",
  "    {\"label\": \"09:00\", \"web\": 1.2, \"db\": 0.8},\n",
  "    {\"label\": \"10:00\", \"web\": 1.9, \"db\": 1.1},\n",
  "    {\"label\": \"11:00\", \"web\": 2.4, \"db\": 1.0}\n",
  "  ]\n",
  "}\n",
  NULL
};

const char *app_demo_deck(void)
{
  static char buffer[3604];
  size_t i,pos=0,n;
  if(buffer[0])return buffer;
  for(i=0;app_demo_lines[i];i++){
    n=strlen(app_demo_lines[i]);
    memcpy(buffer+pos,app_demo_lines[i],n);pos+=n;
  }
  buffer[pos]=0;return buffer;
}
void app_print_example(FILE *f,const char *what)
{
  const char *const *lines;
  size_t i;
  if(!strcmp(what,"deck")||!strcmp(what,"d"))lines=app_deck_lines;
  else if(!strcmp(what,"json")||!strcmp(what,"j"))lines=app_json_lines;
  else {fputs("#chart: type=bar, title=\"Revenue vs costs\", values\n"
              "month,revenue,costs\nJan,120,80\nFeb,145,88\nMar,132,91\nApr,178,104\n",f);return;}
  for(i=0;lines[i];i++)fputs(lines[i],f);
}
