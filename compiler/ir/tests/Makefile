CC = clang++
INCLUDE = -I..
CFLAGS = -Wall -Wextra -Wno-multichar $(INCLUDE)
BUILDDIR = build
SRCDIR = ..

SRCS = $(wildcard $(SRCDIR)/*.cpp)
OBJS = $(addprefix $(BUILDDIR)/,$(SRCS:$(SRCDIR)/%.cpp=%.o))

all: mkdir main

main: $(OBJS) main.cpp
	$(CC) $(CFLAGS) main.cpp $(OBJS) -o $(BUILDDIR)/main

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

mkdir:
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

run: all
	$(BUILDDIR)/main

cleanrun: clean run


-include $(OBJS:.o=.d)
