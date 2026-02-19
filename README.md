# ENGLANG — A Turing-Complete Programming Language in Plain English

ENGLANG is a fully working interpreted programming language written in C.
Every statement reads like plain English. It is Turing-complete: it supports
variables, conditionals, loops, functions, arrays, a stack, and raw memory.

---

## Download 

```
git clone https://github.com/arandomdevwhodoesnothing/Englang.git
```

## Build

```bash
make
```

Or manually:
```bash
gcc -O2 -o englang englang.c -lm
```

## Run

```bash
./englang yourscript.eng
```

---

## Language Reference

### Comments
```
# This is a comment
// This also works
```

### Variables

```
set x to 42
set name to "Alice"
set result to x plus 10
set diff to a minus b
set product to x times y
set quotient to a divided by b
set remainder to a modulo b
set power to x power 2
set msg to first concatenated with " " concatenated with last
```

### Arithmetic operations

```
add a and b into result
subtract a from b into result      # result = b - a
multiply a by b into result
divide a by b into result
increment counter
increment counter by 5
decrement counter
decrement counter by 2
square root of x into root
absolute value of x into abs_x
```

### Output & Input

```
print x
print "Hello" and name and "!"
say x
ask "Enter your name:" into name
```

### Conditionals

Operators: `greater than`, `less than`, `equal to`,
           `greater than or equal to`, `less than or equal to`,
           `empty`, `zero`

Add `not` after `is` to negate.

```
if x is greater than 10 then
    print "big"
otherwise
    print "small"
end if

if name is equal to "Alice" then
    print "Hello Alice"
end if

if x is not zero then
    print x
end if
```

### Loops

**While loop:**
```
while x is less than 100 then
    increment x
end while
```

**Repeat N times:**
```
repeat 5 times
    print "hello"
end repeat
```

**For loop:**
```
for i from 1 to 10 step 1 then
    print i
end for

for i from 10 to 1 step -1 then
    print i
end for
```

### Functions

```
define greet with name as
    print "Hello" and name
end define

call greet with "Alice"

define factorial with n as
    set result to 1
    set i to 1
    while i is less than or equal to n then
        multiply result by i into result
        increment i
    end while
    set return to result
end define

call factorial with 5
print return
```

### Arrays

```
create array numbers
append 10 to array numbers
append 20 to array numbers
append 30 to array numbers

size of array numbers into len

get element 0 of array numbers into val
set element 1 of array numbers to 99

for i from 0 to 2 step 1 then
    get element i of array numbers into val
    print val
end for
```

### Stack

```
push 42 onto stack
push 13 onto stack
pop from stack into x       # x = 13
pop from stack into y       # y = 42
```

### Raw Memory

```
store x at address 0
store y at address 1
load from address 0 into a
```

1024 memory cells available (addresses 0–1023).

### String Operations

```
length of msg into len
convert x to string
convert s to number
set s to a concatenated with b
```

### Misc

```
return value        # set the "return" variable inside a function
stop                # terminate the program
exit
```

---

## Examples

| File | Description |
|------|-------------|
| `examples/hello.eng` | Hello World |
| `examples/fibonacci.eng` | Fibonacci sequence |
| `examples/factorial.eng` | Factorial via function |
| `examples/fizzbuzz.eng` | FizzBuzz |
| `examples/primes.eng` | Prime sieve |
| `examples/sort.eng` | Bubble sort on an array |
| `examples/advanced.eng` | Stack calc, memory, strings |

---

## Why Turing-Complete?

ENGLANG has:
- **Unbounded variables** (up to 512, easily extended)
- **Conditional branching** (`if/otherwise/end if`)
- **Arbitrary loops** (`while`) — no bound on iterations
- **Arrays + memory** — arbitrary data storage
- **Functions** — reusable subroutines
- **Stack** — push/pop operations

This is sufficient to simulate any Turing machine.
