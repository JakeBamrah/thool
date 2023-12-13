LIB_NAME = thool.a
FILES := $(wildcard src/*.c)
OBJECTS = $(patsubst %.c, build/%.o, $(FILES))

# build libh as static lib
build/$(LIB_NAME): $(OBJECTS)
	ar rcs $@ $^

build/%.o: $(FILES)
	mkdir -p $(dir $@)
	$(CC) -o $@ $< -c

clean:
	rm -r build
