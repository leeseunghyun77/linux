// gcc -O2 -Wall -Wextra os_rr_wait.c -o os_rr_wait
// ./os_rr_wait
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#define NPROC 10
#define QUANTUM 5
#define TICK_USEC 100000

typedef enum { ST_READY, ST_RUNNING, ST_SLEEP, ST_DONE } state_t;

typedef struct {
    pid_t pid;
    int idx;
    int cpu_burst;
    int quantum_remaining;
    state_t state;
    int sleep_ticks;
    int waiting_time;   // READY 상태에서 기다린 시간
} pcb_t;

pcb_t pcbs[NPROC];
int ready_queue[NPROC], rq_head, rq_tail, rq_count;
int running_idx;
volatile sig_atomic_t tick_flag;
volatile sig_atomic_t last_io_pid;
volatile sig_atomic_t child_exit_flag;

/* 큐 관리 */
void enqueue_ready(int idx){
    ready_queue[rq_tail]=idx;
    rq_tail=(rq_tail+1)%NPROC;
    rq_count++;
}
int dequeue_ready(){
    if(rq_count==0) return -1;
    int idx=ready_queue[rq_head];
    rq_head=(rq_head+1)%NPROC;
    rq_count--;
    return idx;
}

/* 자식 */
void child_loop(int idx){
    srand(time(NULL)^getpid());
    sigset_t set; sigemptyset(&set);
    sigaddset(&set,SIGUSR1); sigaddset(&set,SIGTERM);
    sigprocmask(SIG_BLOCK,&set,NULL);
    for(;;){
        int sig; sigwait(&set,&sig);
        if(sig==SIGTERM) _exit(0);
        if(sig==SIGUSR1){
            // burst 감소는 부모 PCB에서 관리
        }
    }
}

/* 핸들러 */
void on_timer(int s){tick_flag=1;}
void on_io_request(int s,siginfo_t*info,void*ctx){last_io_pid=info->si_pid;}
void on_child_exit(int s){child_exit_flag=1;}

void setup_signals(){
    struct sigaction sa={0};
    sa.sa_handler=on_timer; sigaction(SIGALRM,&sa,NULL);
    struct sigaction ioa={0};
    ioa.sa_sigaction=on_io_request; ioa.sa_flags=SA_SIGINFO;
    sigaction(SIGUSR2,&ioa,NULL);
    struct sigaction cha={0};
    cha.sa_handler=on_child_exit; sigaction(SIGCHLD,&cha,NULL);
    struct itimerval itv={{0,TICK_USEC},{0,TICK_USEC}};
    setitimer(ITIMER_REAL,&itv,NULL);
}

/* 스케줄링 */
void maybe_start_next(){
    if(running_idx!=-1) return;
    int next=dequeue_ready();
    if(next!=-1){
        running_idx=next;
        pcbs[next].state=ST_RUNNING;
        pcbs[next].quantum_remaining=QUANTUM;
    }
}
void deliver_tick(int idx){kill(pcbs[idx].pid,SIGUSR1);}
int all_done(){
    for(int i=0;i<NPROC;i++) if(pcbs[i].state!=ST_DONE) return 0;
    return 1;
}
void schedule_tick(){
    // READY 상태 대기 시간 증가
    for(int i=0;i<NPROC;i++){
        if(pcbs[i].state==ST_READY){
            pcbs[i].waiting_time++;
        }
    }

    // sleep 처리
    for(int i=0;i<NPROC;i++){
        if(pcbs[i].state==ST_SLEEP){
            if(--pcbs[i].sleep_ticks<=0){
                pcbs[i].state=ST_READY;
                enqueue_ready(i);
            }
        }
    }
    maybe_start_next();
    if(running_idx!=-1){
        int idx=running_idx;
        deliver_tick(idx);
        if(pcbs[idx].cpu_burst>0) pcbs[idx].cpu_burst--;
        if(pcbs[idx].quantum_remaining>0) pcbs[idx].quantum_remaining--;

        if(pcbs[idx].cpu_burst==0){
            if(rand()%2){
                pcbs[idx].state=ST_SLEEP;
                pcbs[idx].sleep_ticks=1+rand()%5;
            } else {
                pcbs[idx].state=ST_DONE;
            }
            running_idx=-1;
        }
        else if(pcbs[idx].quantum_remaining==0){
            pcbs[idx].state=ST_READY;
            enqueue_ready(idx);
            running_idx=-1;
        }
    }
    maybe_start_next();
}

/* 메인 */
int main(){
    srand(time(NULL));
    setup_signals();

    while(1){ // 무한 반복
        rq_head=rq_tail=rq_count=0;
        running_idx=-1;
        tick_flag=0; last_io_pid=0; child_exit_flag=0;

        // 자식 생성
        for(int i=0;i<NPROC;i++){
            pid_t pid=fork();
            if(pid==0){child_loop(i); return 0;}
            pcbs[i].pid=pid; pcbs[i].idx=i;
            pcbs[i].cpu_burst=1+rand()%10;
            pcbs[i].quantum_remaining=QUANTUM;
            pcbs[i].state=ST_READY; pcbs[i].sleep_ticks=0;
            pcbs[i].waiting_time=0;
            enqueue_ready(i);
        }

        maybe_start_next();

        // 스케줄링 루프
        while(!all_done()){
            pause();
            if(tick_flag){
                tick_flag=0;
                schedule_tick();
                printf("[tick] ");
                for(int i=0;i<NPROC;i++){
                    char c='?';
                    switch(pcbs[i].state){
                        case ST_READY:c='R';break;
                        case ST_RUNNING:c='X';break;
                        case ST_SLEEP:c='S';break;
                        case ST_DONE:c='D';break;
                    }
                    printf("%d:%c(burst=%d,q=%d",i,c,pcbs[i].cpu_burst,pcbs[i].quantum_remaining);
                    if(pcbs[i].state==ST_SLEEP) printf(",io=%d",pcbs[i].sleep_ticks);
                    printf(") ");
                }
                printf("\n");
            }
        }

        // 각 프로세스 대기 시간 출력
        int total_wait=0;
        printf("=== 프로세스별 READY 큐 대기 시간 ===\n");
        for(int i=0;i<NPROC;i++){
            printf("프로세스 %d: %d ticks\n", i, pcbs[i].waiting_time);
            total_wait += pcbs[i].waiting_time;
        }
        double avg_wait = (double)total_wait / NPROC;
        printf("모든 프로세스 종료. 평균 대기 시간 = %.2f ticks\n", avg_wait);

        sleep(2); // 잠깐 쉬고 다시 반복
    }
    return 0;
}