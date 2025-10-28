# C++20 coroutines playground

This project aims to evaluate the potential benefits and risks of using C++20
coroutines to solve "real-world" problems. Things to think about:

- Self implementation of promises or use a library? Which one?
- Impact on the code readability
- Development costs comparing with classic concurrent/async solutions
- Performance improvement and comparison with classic concurrent/async solutions

## The problem to solve

We start with a simple implementation involving sequential I/O operations using
linux API. The idea is:
- Have a collection of randomly chosen operations, which can be:
    - Read: reads the contents of a random file and returns true if the number
    of digits in it is mod 10
    - Write: Writes a random 1MiB string into a random file.
    - Write in chunks: Writes a random 1MiB string to a random file but
    splitting the writing in a random number of chunks (between 5 and 10). Each
    chunk write is equivalent to a full write operation with the additional step
    of an `lseek`
- Iterate over the collection I times
- In each iteration, execute the operations specified in the collection
- Depending on the result, remove the element from the collection
- At the end of each iteration, re-fill it with new random operations to
perform in the next one.

From that sequential starting point, the goal is to try to parallelize calls to
read and write. But only those calls to the linux API while ensuring the rest is
executed in the same thread. We can interprete this as a situation where we want
to ensure single-thread access to some information but we do allow concurrent IO
calls.

## Preliminar results

### Parallelize only read operations

Execution times for the three implementations with 200 operations and 10
iterations. In async and coro versions, only read operations were parallelized.

```
Async - Execution time: 526 ms
Coro - Execution time: 521 ms
Sequential - Execution time: 1638 ms
```

Feelings:
- Very similar times for both concurrent implementations, i.e. no performance
degradations due to the usage of coroutines.
- Almost trivial refactor from sequential to coro
- A little bit harder refactor for async but not the worst
