all: process.c mmu.c master.c sched.c
	gcc master.c -o master -lm
	gcc sched.c -o scheduler -lm
	gcc mmu.c -o mmu -lm
	gcc process.c -o process -lm

clean:
	rm process master scheduler mmu