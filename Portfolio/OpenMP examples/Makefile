CC= gcc
CFLAGS= -g -Wall -O2 -fopenmp

OBJS_MAIN21 = main21.o
EXEC_MAIN21 = main21

OBJS_SUBST= back_subst.o back_subst_modl.o
EXEC_SUBST= back_subst

#########################################################
$(EXEC_MAIN21): $(OBJS_MAIN21)
	$(CC) $(OBJS_MAIN21) $(CFLAGS) -o $@

$(EXEC_SUBST): $(OBJS_SUBST)
	$(CC) $(OBJS_SUBST) $(CFLAGS) -o $@



main21.o: main21.c
	$(CC) -c $< $(CFLAGS)

back_subst.o: back_subst.c back_subst.h
	$(CC) -c $< $(CFLAGS)

back_subst_modl.o: back_subst_modl.c back_subst.h
	$(CC) -c $< $(CFLAGS)

#########################################################
clean_main21:
	rm -f $(OBJS_MAIN21) $(EXEC_MAIN21)

clean_subst:
	rm -f $(OBJS_SUBST) $(EXEC_SUBST)

clean:
	make clean_main21
	make clean_subst