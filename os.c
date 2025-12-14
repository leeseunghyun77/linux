#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#define NPROC 10
#define QUANTUM 5              // 타임 퀀텀 (조정 가능)
#define TICK_USEC 100000       // 0.1초
#define IO_PROB 50             // burst 완료 시 I/O로 갈 확률(%) 0~100

typedef enum { ST_READY, ST_RUNNING, ST_SLEEP, ST_DONE } state_t;

typedef struct {
    pid_t pid;
    int idx;
    int cpu_burst;
    int quantum_remaining;
    state_t state;
    int sleep_ticks;

    // 성능 지표
    int waiting_time;          // READY 상태에서 대기한 총 시간
    int running_time;          // RUNNING 상태에서 실행한 총 시간
    int io_wait_time;          // SLEEP 상태(I/O 대기) 총 시간
    int arrival_tick;          // READY 큐에 처음 들어온 시간
    int first_run_tick;        // 처음 RUNNING이 된 시간 (응답시간 측정용)
    int completion_tick;       // DONE 시점의 시간
    int responded;             // 처음 실행 여부 플래그
} pcb_t;

pcb_t pcbs[NPROC];
int ready_queue[NPROC], rq_head, rq_tail, rq_count;
int running_idx;
volatile sig_atomic_t tick_flag;
volatile sig_atomic_t last_io_pid;
volatile sig_atomic_t child_exit_flag;

// 시스템 시간
int sys_tick = 0;

// 큐 관리
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

// 자식 프로세스
void child_loop(int idx){
    srand(time(NULL)^getpid());
    sigset_t set; sigemptyset(&set);
    sigaddset(&set,SIGUSR1); sigaddset(&set,SIGTERM);
    sigprocmask(SIG_BLOCK,&set,NULL);
    for(;;){
        int sig; sigwait(&set,&sig);
        if(sig==SIGTERM) _exit(0);
        if(sig==SIGUSR1){
            // 부모가 스케줄링
        }
    }
}

// Handlers
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

void maybe_start_next(){
    if(running_idx!=-1) return;
    int next=dequeue_ready();
    if(next!=-1){
        running_idx=next;
        pcbs[next].state=ST_RUNNING;
        pcbs[next].quantum_remaining=QUANTUM;
        if(!pcbs[next].responded){
            pcbs[next].responded=1;
            pcbs[next].first_run_tick=sys_tick;
        }
    }
}
void deliver_tick(int idx){kill(pcbs[idx].pid,SIGUSR1);}
int all_done(){
    for(int i=0;i<NPROC;i++) if(pcbs[i].state!=ST_DONE) return 0;
    return 1;
}

void schedule_tick(){
    // 성능 지표 수정
    for(int i=0;i<NPROC;i++){
        if(pcbs[i].state==ST_READY) pcbs[i].waiting_time++;
        else if(pcbs[i].state==ST_SLEEP) pcbs[i].io_wait_time++;
    }
    if(running_idx!=-1){
        pcbs[running_idx].running_time++;
    }

    // I/O 발생 시 대기 시간
    for(int i=0;i<NPROC;i++){
        if(pcbs[i].state==ST_SLEEP){
            if(--pcbs[i].sleep_ticks<=0){
                pcbs[i].state=ST_READY;
                enqueue_ready(i);
            }
        }
    }

    // 한 틱마다 프로세스 실행
    maybe_start_next();
    if(running_idx!=-1){
        int idx=running_idx;
        deliver_tick(idx);

        // cpu버스트, 퀀텀 감소
        if(pcbs[idx].cpu_burst>0) pcbs[idx].cpu_burst--;
        if(pcbs[idx].quantum_remaining>0) pcbs[idx].quantum_remaining--;

        // cpu 버스트 0일 때 I/O 또는 종료료
        if(pcbs[idx].cpu_burst==0){
            int r = rand()%100;
            if(r < IO_PROB){
                pcbs[idx].state=ST_SLEEP;
                pcbs[idx].sleep_ticks=1+rand()%5;
            } else {
                pcbs[idx].state=ST_DONE;
                pcbs[idx].completion_tick=sys_tick+1;
            }
            running_idx=-1;
        }
        // 퀀텀이 0이고 cpu 버스트 0이 아닐 때
        else if(pcbs[idx].quantum_remaining==0){
            pcbs[idx].state=ST_READY;
            enqueue_ready(idx);
            running_idx=-1;
        }
    }

    sys_tick++;
    maybe_start_next();
}

void print_state_line(){
    printf("[tick %d] ", sys_tick);
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

/* Main */
int main(){
    srand(time(NULL));
      srand(time(NULL)    while(1){ // 무한 반복
        // 초기화
        sys_tick=0;
        rq_head=rq_tail=rq_count=0;
        running_idx=-1;
        tick_flag=0; last_io_pid=0; child_exit_flag=0;

        // 자식 프로세스 생성 및 초기화
        for(int i=0;i<NPROC;i++){
            pid_t pid=fork();
            if(pid==0){child_loop(i); return 0;}
            pcbs[i].pid=pid; pcbs[i].idx=i;
            pcbs[i].cpu_burst=1+rand()%10;          
            pcbs[i].quantum_remaining=QUANTUM;
            pcbs[i].state=ST_READY; pcbs[i].sleep_ticks=0;
            pcbs[i].waiting_time=0;
            pcbs[i].running_time=0;
            pcbs[i].io_wait_time=0;
            pcbs[i].arrival_tick=sys_tick;          
            pcbs[i].first_run_tick=-1;
            pcbs[i].completion_tick=-1;
            pcbs[i].responded=0;
            enqueue_ready(i);
        }

        maybe_start_next();

        // Scheduling loop
        while(!all_done()){
            pause();
            if(tick_flag){
                tick_flag=0;
                schedule_tick();
                print_state_line();
            }
        }

        // Metrics aggregation
        int total_wait=0, total_resp=0, total_turn=0;
        long long total_running_ticks=0, total_io_ticks=0;

        for(int i=0;i<NPROC;i++){
            total_wait += pcbs[i].waiting_time;
            total_running_ticks += pcbs[i].running_time;
            total_io_ticks += pcbs[i].io_wait_time;

            int resp = (pcbs[i].first_run_tick>=0) ? (pcbs[i].first_run_tick - pcbs[i].arrival_tick) : 0;
            int turn = (pcbs[i].completion_tick>=0) ? (pcbs[i].completion_tick - pcbs[i].arrival_tick) : sys_tick - pcbs[i].arrival_tick;
            total_resp += resp;
            total_turn += turn;
        }

        double avg_wait = (double)total_wait / NPROC;
        double avg_resp = (double)total_resp / NPROC;
        double avg_turn = (double)total_turn / NPROC;

        // CPU utilization: fraction of time CPU executed a process
        // total system time = sys_tick ticks
        double cpu_util = (sys_tick>0) ? (100.0 * (double)total_running_ticks / (double)sys_tick) : 0.0;

        // I/O waiting ratio: fraction of process-time spent in I/O wait
        // normalize by NPROC * sys_tick to get average per-process fraction
        double io_wait_ratio = (NPROC>0 && sys_tick>0) ? (100.0 * (double)total_io_ticks / (double)(NPROC * sys_tick)) : 0.0;

        // Per-process metrics
        printf("\n=== 프로세스별 지표 ===\n");
        for(int i=0;i<NPROC;i++){
            int resp = (pcbs[i].first_run_tick>=0) ? (pcbs[i].first_run_tick - pcbs[i].arrival_tick) : 0;
            int turn = (pcbs[i].completion_tick>=0) ? (pcbs[i].completion_tick - pcbs[i].arrival_tick) : sys_tick - pcbs[i].arrival_tick;
            double io_ratio = (sys_tick>0) ? (100.0 * (double)pcbs[i].io_wait_time / (double)sys_tick) : 0.0;

            printf("P%d: wait=%d, resp=%d, turn=%d, run=%d, io_wait=%d (io_ratio=%.1f%%)\n",
                   i,
                   pcbs[i].waiting_time,
                   resp,
                   turn,
                   pcbs[i].running_time,
                   pcbs[i].io_wait_time,
                   io_ratio);
        }

        // Averages
        printf("\n=== 평균 지표 ===\n");
        printf("평균 대기 시간: %.2f ticks\n", avg_wait);
        printf("평균 응답 시간: %.2f ticks\n", avg_resp);
        printf("평균 반환(턴어라운드) 시간: %.2f ticks\n", avg_turn);
        printf("CPU 이용률: %.2f%%\n", cpu_util);
        printf("I/O 대기 비율(평균): %.2f%%\n", io_wait_ratio);

        puts("\n모든 프로세스 종료. 2초 후 새로운 사이클을 시작합니다.\n");
        sleep(2);
    }
    return 0;
}


