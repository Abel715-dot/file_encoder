#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TASK_SIZE (64UL * 1024UL)
#define TASK_SENTINEL ((size_t)-1)
#define Q_CAP 128

typedef struct {
    uint8_t ch;
    size_t cnt;
} Pair;

typedef struct {
    size_t id;
    size_t start;
    size_t num_bytes;
    const uint8_t *data;
    Pair *pairs;
    size_t pairs_count;
    size_t max_pairs_count;
} Task;

Task *tasks;
size_t task_count;
pthread_mutex_t lock;
pthread_cond_t q_not_empty;
pthread_cond_t pool_cv;

size_t q_buf[Q_CAP];
size_t q_head;
size_t q_tail;
size_t q_count;

int *task_done;

int has_pending;
uint8_t pending_ch;
size_t pending_cnt;

void emit_pair(uint8_t ch, uint8_t cnt) {
    uint8_t out[2];
    out[0] = ch;
    out[1] = cnt;
    fwrite(out, 1, 2, stdout);
}

void append_pair(Task *task, uint8_t ch, size_t cnt) {
    //append new pair or extend original last pair within the task
    if (cnt == 0) {
        return;
    }
    if (task->pairs_count > 0 && task->pairs[task->pairs_count - 1].ch == ch) {
        task->pairs[task->pairs_count - 1].cnt += cnt;
        return;
    }
    if (task->pairs_count == task->max_pairs_count) {
        //we cannot initialize within the struct, so that
        //when max_pairs_count==0, it means we've not allocated
        //pairs yet.
        size_t new_cap =
            (task->max_pairs_count == 0) ? 16 : task->max_pairs_count * 2;
        task->pairs = realloc(task->pairs, new_cap * sizeof(Pair));
        task->max_pairs_count = new_cap;
    }
    task->pairs[task->pairs_count].ch = ch;
    task->pairs[task->pairs_count].cnt = cnt;
    task->pairs_count++;
}

void encode_task(Task *task) {
    if (task->num_bytes == 0) {
        return;
    }

    uint8_t prev = task->data[task->start];
    //first byte of this task
    size_t cnt = 1;
    for (size_t i = 1; i < task->num_bytes; i++) {
        uint8_t c = task->data[task->start + i];
        if (c == prev) {
            cnt++;
        } else {
            append_pair(task, prev, cnt);
            prev = c;
            cnt = 1;
        }
    }
    append_pair(task, prev, cnt);
}

void submit_task_locked(Task *task) {
    for (size_t i = 0; i < task->pairs_count; ++i) {
        Pair curr_pair = task->pairs[i];
        if (!has_pending) {
            pending_ch = curr_pair.ch;
            pending_cnt = curr_pair.cnt;
            has_pending = 1;
            continue;
        }
        if (pending_ch == curr_pair.ch) {
            pending_cnt += curr_pair.cnt;
            continue;
        }
        emit_pair(pending_ch, (uint8_t)pending_cnt);
        pending_ch = curr_pair.ch;
        pending_cnt = curr_pair.cnt;
    }
}

void *worker(void *arg) {
    (void)arg;
    for (;;) {
        
        pthread_mutex_lock(&lock);
        while (q_count == 0) {
            pthread_cond_wait(&q_not_empty, &lock);
        }
        size_t task_id = q_buf[q_head];
        q_head = (q_head + 1) % Q_CAP;
        q_count--;
        pthread_cond_signal(&pool_cv);
        pthread_mutex_unlock(&lock);

        if (task_id == TASK_SENTINEL) {
            return NULL;
        }

        encode_task(&tasks[task_id]);

        pthread_mutex_lock(&lock);
        task_done[task_id] = 1;
        //意味着任务完成
        pthread_cond_signal(&pool_cv);
        pthread_mutex_unlock(&lock);
    }
}

int main(int argc, char **argv) {
    int thread_count = 1;
    int file_start = 1;

    if (argc >= 2 &&
        (strcmp(argv[1], "j") == 0 || strcmp(argv[1], "-j") == 0)) {
        if (argc < 4) {
            fprintf(stderr,
                    "usage: %s [-j|j] <num_threads> <file> [...]\n",
                    argv[0]);
            return 1;
        }
        thread_count = atoi(argv[2]);
        if (thread_count < 1) {
            thread_count = 1;
        }
        file_start = 3;
    }

    int file_count = argc - file_start;//计算文件数量
    if (file_count < 1) {
        fprintf(stderr, "usage: %s <file> [...]\n", argv[0]);
        fprintf(stderr,
                "       %s [-j|j] <num_threads> <file> [...]\n",
                argv[0]);
        return 1;
    }

    size_t *file_sizes = calloc((size_t)file_count, sizeof(size_t));
    //记录每个文件size的“列表”
    // 等效做法（stack数组，而不用heap的方式）：size_t file_sizes[file_count];
    //局限性：当file_count 很大的时候，容易stack溢出。用heap更好
    const uint8_t **file_maps = calloc((size_t)file_count, sizeof(*file_maps));
    //*file_map 的含义在第195行。*file_map的类型是：*const uint8_t
    task_count = 0;

    for (int i = 0; i < file_count; i++) {
        int fd = open(argv[file_start + i], O_RDONLY);
        if (fd < 0) {
            perror("open");
            return 1;
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            perror("fstat");
            close(fd);
            return 1;
        }


        //这里在更新file_sizes 列表
        size_t sz = (size_t)st.st_size;
        file_sizes[i] = sz;
        if (sz > 0) {
            void *map = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
            if (map == MAP_FAILED) {
                perror("mmap");
                close(fd);
                return 1;
            }
            file_maps[i] = (const uint8_t *)map;
            //map 类型是void*, 因此需要强制转换
            task_count += (sz + TASK_SIZE - 1) / TASK_SIZE;
            //task_count 动态记录
        } else {
            file_maps[i] = NULL;
        }

        close(fd);
    }

    tasks = calloc(task_count, sizeof(Task));

    size_t tid = 0;
    for (int i = 0; i < file_count; i++) {
        size_t sz = file_sizes[i];                                                                    
        for (size_t start = 0; start < sz; start += TASK_SIZE) {
            size_t remain = sz - start;
            size_t len = (remain < TASK_SIZE) ? remain : TASK_SIZE;
            tasks[tid].id = tid;
            tasks[tid].data = file_maps[i];
            //所有files都指向filei的第一个byte，后续用他们的start来调整具体处理什么
            tasks[tid].start = start;
            tasks[tid].num_bytes = len;
            tid++;
        }
    }

    has_pending = 0;
    q_head = 0;
    q_tail = 0;
    q_count = 0;
    task_done = calloc(task_count, sizeof(int));
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&q_not_empty, NULL);
    pthread_cond_init(&pool_cv, NULL);

    pthread_t *threads = calloc((size_t)thread_count, sizeof(pthread_t));
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    size_t merged = 0;
    size_t next_submit = 0;
    if (task_count > 0) {
        while (merged < task_count) {
            pthread_mutex_lock(&lock);
            while (next_submit < task_count && q_count < Q_CAP) {
                q_buf[q_tail] = next_submit++;
                q_tail = (q_tail + 1) % Q_CAP;
                q_count++;
                pthread_cond_signal(&q_not_empty);
                //可能有子线程由于任务列表是空，在睡觉。这是一个让他们recheck他们条件的唤醒信号
            }
            while (merged < task_count && task_done[merged]) {
                pthread_mutex_unlock(&lock);
                submit_task_locked(&tasks[merged]);
                merged++;
                pthread_mutex_lock(&lock);
            }
            if (merged >= task_count) {
                pthread_mutex_unlock(&lock);
                break;
            }
            while (!((next_submit < task_count && q_count < Q_CAP) ||
                     task_done[merged])) {
                pthread_cond_wait(&pool_cv, &lock);
            }
            pthread_mutex_unlock(&lock);
        }
    }

    size_t sent_left = (size_t)thread_count;
    pthread_mutex_lock(&lock);
    while (sent_left > 0) {
        while (q_count >= Q_CAP) {
            pthread_cond_wait(&pool_cv, &lock);
        }
        q_buf[q_tail] = TASK_SENTINEL;
        q_tail = (q_tail + 1) % Q_CAP;
        q_count++;
        sent_left--;
        pthread_cond_signal(&q_not_empty);
    }
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    if (has_pending) {
        emit_pair(pending_ch, (uint8_t)pending_cnt);
    }
    fflush(stdout);

    for (size_t i = 0; i < task_count; i++) {
        free(tasks[i].pairs);
    }
    free(threads);
    free(task_done);
    free(tasks);
    for (int i = 0; i < file_count; i++) {
        if (file_sizes[i] > 0 && file_maps[i] != NULL) {
            munmap((void *)file_maps[i], file_sizes[i]);
        }
    }
    free(file_maps);
    free(file_sizes);
    pthread_cond_destroy(&pool_cv);
    pthread_cond_destroy(&q_not_empty);
    pthread_mutex_destroy(&lock);

    return 0;
}
