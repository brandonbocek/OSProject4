all: oss user clock log.out

log.out:
	touch log.out

oss:
	gcc -o oss oss.c

user:
	gcc -o user user.c

clock:
	gcc -o clock virtualclock.c

clean:
	rm -f oss
	rm -f user
	rm -f clock
	rm -f *.out
