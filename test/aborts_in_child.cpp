#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

/* A test that expects the process to end runs its part in a child
 * process, so the test run itself goes on. It answers whether the child
 * ended with SIGABRT. */
bool aborts_in_child(void (*const run)())
{
    const pid_t child = fork();
    if (child == 0) {
        run();
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
