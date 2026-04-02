# Host test — compiles without ESP-IDF
CC = g++
CFLAGS = -Wall -Wextra -DNATIVE_TEST -I../../components/unity/include
UNITY_SRC = ../../components/unity/src/unity.c

# If Unity not available locally, download it
unity.c:
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.c -o unity.c
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.h -o unity.h
	curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity_internals.h -o unity_internals.h

test: main/test_vrm_topics.cpp unity.c
	$(CC) $(CFLAGS) -I. -o test_vrm_topics main/test_vrm_topics.cpp unity.c
	./test_vrm_topics

clean:
	rm -f test_vrm_topics unity.c unity.h unity_internals.h
