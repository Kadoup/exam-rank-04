#include <unistd.h>     // fork(), alarm(), _exit()
#include <sys/wait.h>   // waitpid(), WIFEXITED, WIFSIGNALED, WEXITSTATUS, WTERMSIG
#include <signal.h>     // sigaction, SIGALRM, SIGKILL
#include <stdbool.h>    // bool
#include <stdio.h>      // printf()
#include <stdlib.h>     // exit()
#include <errno.h>      // errno, EINTR
#include <string.h>     // strsignal()

// Empty handler so waitpid() is interrupted by SIGALRM
static void do_nothing(int sig)
{
    (void)sig;
}

int sandbox(void (*f)(void), unsigned int timeout, bool verbose)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0)
    {
        // Child: run function, but do NOT set alarm here.
        f();
        _exit(0);
    }

    // --- Parent here ---

    // Install SIGALRM handler to interrupt waitpid()
    struct sigaction sa = {0};
    sa.sa_handler = do_nothing;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) < 0)
        return -1;

    // Start timeout
    alarm(timeout);

    int status;
    pid_t r = waitpid(pid, &status, 0);

    if (r < 0)
    {
        if (errno == EINTR)
        {
            // Timeout
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);   // reap child

            if (verbose)
                printf("Bad function: timed out after %u seconds\n", timeout);

            return 0;
        }
        return -1;
    }

    // Child ended before timeout
    alarm(0);

    if (WIFEXITED(status))
    {
        int code = WEXITSTATUS(status);
        if (code == 0)
        {
            if (verbose)
                printf("Nice function!\n");
            return 1;
        }
        if (verbose)
            printf("Bad function: exited with code %d\n", code);
        return 0;
    }

    if (WIFSIGNALED(status))
    {
        int sig = WTERMSIG(status);

        if (sig == SIGALRM)
        {
            if (verbose)
                printf("Bad function: timed out after %u seconds\n", timeout);
        }
        else
        {
            const char *desc = strsignal(sig);
            if (!desc)
                desc = "Unknown signal";
            if (verbose)
                printf("Bad function: %s\n", desc);
        }
        return 0;
    }

    // Unexpected status
    return -1;
}