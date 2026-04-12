# Host test — compiles without ESP-IDF
CC = g++
CFLAGS = -Wall -Wextra -DNATIVE_TEST -I../../components/unity/include
UNITY_SRC = ../../components/unity/src/unity.c

# If Unity not available locally, download it
unity.c:
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.c -o unity.c
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.h -o unity.h
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity_internals.h -o unity_internals.h

test: main/test_protection.cpp unity.c
	$(CC) $(CFLAGS) -I. -o test_protection main/test_protection.cpp unity.c
	./test_protection

clean:
	rm -f test_protection unity.c unity.h unity_internals.h
