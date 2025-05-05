CFLAGS = -Wall -lpthread

OBJS_EX1= ex1.o
EXEC_EX1= ex1

OBJS_EX2= ex2.o
EXEC_EX2= ex2

OBJS_EX2ATOM= ex2atom.o
EXEC_EX2ATOM= ex2atom

OBJS_EX3= ex3.o
EXEC_EX3= ex3

OBJS_EX3OPT= ex3opt.o
EXEC_EX3OPT= ex3opt

OBJS_EX4= ex4.o my_rand.o
EXEC_EX4= ex4


#########################################################
$(EXEC_EX1): $(OBJS_EX1)
	gcc $(OBJS_EX1) $(CFLAGS) -o $@

$(EXEC_EX2): $(OBJS_EX2)
	gcc $(OBJS_EX2) $(CFLAGS) -o $@

$(EXEC_EX2ATOM): $(OBJS_EX2ATOM)
	gcc $(OBJS_EX2ATOM) $(CFLAGS) -o $@

$(EXEC_EX3): $(OBJS_EX3)
	gcc $(OBJS_EX3) $(CFLAGS) -o $@

$(EXEC_EX3OPT): $(OBJS_EX3OPT)
	gcc $(OBJS_EX3OPT) $(CFLAGS) -o $@

$(EXEC_EX4): $(OBJS_EX4)
	gcc $(OBJS_EX4) $(CFLAGS) -o $@


#########################################################
clean1:
	rm -f $(OBJS_EX1) $(EXEC_EX1)

clean2:
	rm -f $(OBJS_EX2) $(EXEC_EX2)

clean2atom:
	rm -f $(OBJS_EX2ATOM) $(EXEC_EX2ATOM)

clean3:
	rm -f $(OBJS_EX3) $(EXEC_EX3)

clean3opt:
	rm -f $(OBJS_EX3OPT) $(EXEC_EX3OPT)

clean4:
	rm -f $(OBJS_EX4) $(EXEC_EX4)


clean:
	make clean1
	make clean2
	make clean2atom
	make clean3
	make clean3opt
	make clean4