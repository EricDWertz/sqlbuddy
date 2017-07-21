sqlbuddy: main.c
	gcc -g -o sqlbuddy `pkg-config --cflags --libs gtk+-3.0` `mysql_config --cflags --libs` main.c
