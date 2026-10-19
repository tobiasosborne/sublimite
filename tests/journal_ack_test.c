/* edit-fi0p F12. (P) The reserved legacy name has PID/nonce/attempt fields
 * and a set-aside suffix; unit fields are the smallest positive schema values.
 * The orphan is JOURNAL_ARTIFACT_BYTES + the smallest positive byte increment,
 * so removing the acknowledged source permits the existing cap to reclaim it.
 * Zero/one denote success/failure, one buffer/worker, empty offsets and owner
 * read/write mode is POSIX 0600. Literal bytes supply all fixture lengths. */
#include "journal/journal.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exercise the CLI parser in the sanitizer binary too, without requiring a
 * separately built release tool when make check starts from a clean tree. */
#define main journal_replay_main
#include "../tools/journal_replay.c"
#undef main

#define T(c) do { if (!(c)) { fprintf(stderr, "journal_ack_test:%d FAIL %s\n", __LINE__, #c); rc = 1; goto done; } } while (0)
static int acknowledge(const char *source)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        char *args[] = {"journal_replay", "--acknowledge", (char *)source, NULL};
        /* (P) argc counts the program, flag and pathname. */
        int result = journal_replay_main(3, args);
        (void)fflush(stdout); (void)fflush(stderr); _exit(result);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}
static void cleanup(const char *root)
{
    DIR *dir = opendir(root); if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        (void)unlinkat(dirfd(dir), entry->d_name, 0);
    closedir(dir); (void)rmdir(root);
}
int main(void)
{
    char root[] = "build/journal-fi0p-ack-XXXXXX";
    int rc = 0, fd = -1, guard = -1; bool pool_ready = false;
    work_pool pool; journal *j = NULL; FILE *out = NULL;
    T(mkdtemp(root));
    char source[sizeof ((journal_base *)0)->captured_path], orphan[sizeof source], target[sizeof source], ordinary[sizeof source], alias[sizeof source];
    T(snprintf(source, sizeof source, "%s/.sublimite-journal-!1-1-1.set-aside-1", root) > 0);
    T(snprintf(orphan, sizeof orphan, "%s/.sublimite-base-orphan", root) > 0);
    T(snprintf(target, sizeof target, "%s/resolved", root) > 0);
    T(snprintf(ordinary, sizeof ordinary, "%s/session-preserve", root) > 0);
    /* (P) next suffix distinguishes the alias of the same resolved source. */
    T(snprintf(alias, sizeof alias, "%s/.sublimite-journal-!1-1-1.set-aside-2", root) > 0);
    T(work_pool_init(&pool, 1, 0) == 0); pool_ready = true;
    T(journal_open(&j, source, &pool, NULL) == 0);
    journal_base empty = {.path = ""}; T(journal_set_base(j, 1, &empty) == 0);
    T(journal_insert(j, 1, 0, (const uint8_t *)"recovered", strlen("recovered")) == 0);
    T(journal_flush(j) == 0); journal_close(j); j = NULL;
    work_pool_shutdown(&pool); pool_ready = false;
    fd = open(orphan, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600); T(fd >= 0);
    T(ftruncate(fd, (off_t)(JOURNAL_ARTIFACT_BYTES + 1u)) == 0); close(fd); fd = -1;
    fd = open(target, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600); T(fd >= 0); close(fd); fd = -1;
    out = tmpfile(); T(out);
    T(journal_manual_replay(source, target, 0, true, out) == 0); fclose(out); out = NULL;
    T(journal_retire_directory(root, NULL) == 0 && access(orphan, F_OK) == 0);
    guard = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC); T(guard >= 0 && flock(guard, LOCK_EX | LOCK_NB) == 0);
    T(acknowledge(source) != 0 && access(source, F_OK) == 0);
    T(journal_acknowledge(source) == JOURNAL_BUSY);
    close(guard); guard = -1;
    fd = open(source, O_RDONLY | O_CLOEXEC); T(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    T(acknowledge(source) != 0 && access(source, F_OK) == 0); close(fd); fd = -1;
    T(link(source, ordinary) == 0);
    T(acknowledge(ordinary) != 0 && access(ordinary, F_OK) == 0); T(unlink(ordinary) == 0);
    T(link(source, alias) == 0);
    T(acknowledge(source) == 0 && access(source, F_OK) != 0);
    T(journal_acknowledge(alias) == 0 && access(alias, F_OK) != 0);
    T(journal_retire_directory(root, NULL) == 0 && access(orphan, F_OK) != 0);
    fd = open(target, O_RDONLY | O_CLOEXEC); T(fd >= 0);
    char bytes[sizeof "recovered"]; ssize_t n = read(fd, bytes, sizeof bytes);
    T(n == (ssize_t)strlen("recovered") && !memcmp(bytes, "recovered", strlen("recovered")));
    close(fd); fd = -1;
    T(symlink("resolved", source) == 0);
    struct stat st;
    T(journal_acknowledge(source) != 0 && lstat(source, &st) == 0 && S_ISLNK(st.st_mode));
    T(unlink(source) == 0);
    fd = open(source, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600); T(fd >= 0);
    T(write(fd, "torn", strlen("torn")) == (ssize_t)strlen("torn")); close(fd); fd = -1;
    T(journal_acknowledge(source) != 0 && access(source, F_OK) == 0);
done:
    if (out) fclose(out);
    if (guard >= 0) close(guard);
    if (fd >= 0) close(fd);
    if (j) journal_close(j);
    if (pool_ready) work_pool_shutdown(&pool);
    cleanup(root);
    if (!rc) puts("journal_ack_test: explicit verified acknowledgement releases reserved source and orphan BASE cleanup");
    return rc;
}
