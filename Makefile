all: oss user clock log.out logOSS.out

log.out:
	touch log.out

logOSS.out:
	touch logOSS.out

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
