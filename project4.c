#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/syscall.h>

//gcc -g -Wall -pthread project4.c -lpthread -o project4

void *sig1_thread(void *args);
void *sig2_thread(void *args);
void report_thread();
void sig_generator(int send1, int send2);
void sig_handle_process();
void parent_process(int send1, int send2, char *timer);
void block_sigusr1();
void block_sigusr2();
void siguser1_handler(int num);
void siguser2_handler(int num);

int sig1_recieve_count = 0;
int sig2_recieve_count = 0;
time_t start;
pthread_mutex_t sig1_recieve;
pthread_mutex_t sig2_recieve;

//This structure keeps track of our counter and key
struct shared_val
{
    int value;             // value to increment
    pthread_mutex_t mutex; // lock for this value
};
//This structure will be used for the reporting to print to the log
struct Entry
{
    time_t arrivalTime;
    int num;
    pid_t id;
    int type;
};
typedef struct Entry Entry;

void removeEntry(Entry *array, int index, int array_length);
void addEntry(Entry *array, Entry newJob, int array_length);
int checkSize(Entry array[], int size);

Entry log_array[100];
pthread_mutex_t log_array_lock;

//In here it splits into 4 seperate programs
int main(int argc, char *argv[])
{
    //I get the start time which I will use in parent to know when it has been 30 seconds
    time(&start);
    //This catches if the user didn't enter how they want to run the program
    if (argv[1] == NULL)
    {
        //If they want to run it for 10000 signals they type 1 and if they want to
        //run it for 30 secons they do 2
        printf("Type \"./project4 1\" for 10000 signals or \"./project4 2\" for 30 seconds\n");
        exit(1);
    }

    //This sets up the shared counter for signal 1
    //Sets up a shared storage system
    int sig1_send_count = shmget(IPC_PRIVATE, sizeof(struct shared_val), IPC_CREAT | 0666);
    //Also applies it to the struct which we will pass to our program
    struct shared_val *sig1_send_counts = (struct shared_val *)shmat(sig1_send_count, NULL, 0);
    pthread_mutex_t sig1_send;
    //Initialize stuff
    sig1_send_counts->value = 0;
    sig1_send_counts->mutex = sig1_send;

    //Same initialization method as the first shared counter
    int sig2_send_count = shmget(IPC_PRIVATE, sizeof(struct shared_val), IPC_CREAT | 0666);
    struct shared_val *sig2_send_counts = (struct shared_val *)shmat(sig2_send_count, NULL, 0);
    pthread_mutex_t sig2_send;
    sig2_send_counts->value = 0;
    sig2_send_counts->mutex = sig2_send;

    //Make the first program
    int pid1 = fork();
    if (pid1 < 0)
    { //Error catch
        printf("Fork failed\n");
    }
    else if (pid1 == 0)
    {//Set up the signal mask
        block_sigusr1();
        block_sigusr2();
        //Runs a signal generating program
        sig_generator(sig1_send_count, sig2_send_count);
    }
    else
    {
        //Fork program 2
        int pid2 = fork();
        if (pid2 < 0)
        {//Error catch
            printf("Fork failed\n");
        }
        else if (pid2 == 0)
        {
            //Signal mask
            block_sigusr1();
            block_sigusr2();
            //Second signal generator program
            sig_generator(sig1_send_count, sig2_send_count);
        }
        else
        {
            //Split for third program
            int pid3 = fork();
            if (pid3 < 0)
            {//Error catch
                printf("Fork failed\n");
            }
            else if (pid3 == 0)
            {
                //This will launch the handling program
                sig_handle_process();
            }
            else
            {
                //Then we block the signals in the parent
                block_sigusr1();
                block_sigusr2();
                //Then run the parent process
                parent_process(sig1_send_count, sig2_send_count, argv[1]);
            }
        }
    }
}

//This program catches the SIGUSR1 signals
void *sig1_thread(void *args)
{
    //Lets user know it was made
    printf("sig1 thread made\n");
    //Block SIGUSR2
    block_sigusr2();
    //Setup signal handler for SIGUSR1
    signal(SIGUSR1, siguser1_handler);

    while (1)
    { //This just loops so the thread doesn't end
        sleep(1);
    }
}
//This thread catches the SIGUSR2
void *sig2_thread(void *args)
{
    //Lets user know it was made
    printf("sig2 thread made\n");
    //Block SIGUSR1
    block_sigusr1();
    //Set up signal handler for SIGUSR2
    signal(SIGUSR2, siguser2_handler);
    while (1)
    { //Just loops
        sleep(1);
    }
}

//This thread writes the signals to a log file in groups of 16
void report_thread()
{
    //Initializes the report thread and some values we will us in it
    int j = 1;
    printf("report thread made\n");
    char buffer[30];
    float sig1_average;
    float sig2_average;
    int sig1_amount;
    int sig2_amount;
    int sig1_least_time;
    int sig2_least_time;
    int sig1_most_time;
    int sig2_most_time;
    FILE *fp;
    int size;
    while (1)
    {
        sleep(1);

        //locks the array of signals
        pthread_mutex_lock(&log_array_lock);
        //Then we check if the array has hit the size of 16
        size = checkSize(log_array, 99);
        if (size > 16)
        {//If it has then we will make a log entry for the 16 signals
            //We set the averages, amounts, and times
            sig1_average = 0;
            sig2_average = 0;
            sig1_amount = 0;
            sig2_amount = 0;
            sig1_least_time = log_array[0].arrivalTime;
            sig2_least_time = log_array[0].arrivalTime;
            sig1_most_time = log_array[0].arrivalTime;
            sig2_most_time = log_array[0].arrivalTime;
            //Then we open up the log text file
            fp = fopen("project4_thread_log.txt", "a+");
            //Write the loop number
            fprintf(fp, "Group of signals number %d:\n", j);
            for (int x = 0; x < 16; x++)
            {
                //If its type one
                if (log_array[0].type == 1)
                {//We log it as SIGUSR1
                    if (log_array[0].arrivalTime < sig1_least_time)
                    {//We also set the max time and least time
                        sig1_least_time = log_array[0].arrivalTime;
                    }
                    else if (log_array[0].arrivalTime > sig1_most_time)
                    {
                        sig1_most_time = log_array[0].arrivalTime;
                    }
                    //Then we increase the amount of SIGUSR1s we got in this program
                    sig1_amount++;
                }
                else
                {//We log it as SIGUSR2
                    if (log_array[0].arrivalTime < sig2_least_time)
                    {//We also set the max time and least time
                        sig2_least_time = log_array[0].arrivalTime;
                    }
                    else if (log_array[0].arrivalTime > sig2_most_time)
                    {
                        sig2_most_time = log_array[0].arrivalTime;
                    }
                    //Then we increase the amount of SIGUSR2s we got in this program
                    sig2_amount++;
                }
                //Then we log each part of the signal entry like
                //Which number it is
                fprintf(fp, "Signal Number: %d |", log_array[0].num);
                //What type it was
                fprintf(fp, "Signal Type: SIGUSR%d |", log_array[0].type);
                //Then the arrival time
                strcpy(buffer, asctime(localtime(&log_array[0].arrivalTime)));
                buffer[strcspn(buffer, "\n")] = 0;
                fprintf(fp, "Arrival Time: %s |", buffer);
                //Then thread id
                fprintf(fp, "Thread Id: %ld |\n", (long int)log_array[0].id);
                //then we remove the entry
                removeEntry(log_array, 0 , 99);
                //and loop back to the top
                
            }
            //Then we calculate average time for each signal
            sig1_average = (((float)(sig1_most_time - sig1_least_time)) + 1.0) / ((float)sig1_amount);
            sig2_average = (((float)(sig2_most_time - sig2_least_time)) + 1.0) / ((float)sig2_amount);
            //The we log the average time
            fprintf(fp, "Average Time Between SIGUSR1: %.4f seconds\n", sig1_average);
            fprintf(fp, "Average Time Between SIGUSR2: %.4f seconds\n", sig2_average);
            fprintf(fp, "---------------------------------------------------------------------------------------\n");
            //Increment the loop number
            j++;
            size = 0;
            //And close the file
            fclose(fp);
        }
        //Finally unlocking the array
        pthread_mutex_unlock(&log_array_lock);
    }
}
//This generates random signals at random times
void sig_generator(int send1, int send2)
{
    //Initialize all the values
    struct shared_val *sig1_send_count = (struct shared_val *)shmat(send1, NULL, 0);
    struct shared_val *sig2_send_count = (struct shared_val *)shmat(send2, NULL, 0);
    pthread_mutex_t sig1_send = sig1_send_count->mutex;
    pthread_mutex_t sig2_send = sig2_send_count->mutex;
    //We also sleep for a second so all the masks and handlers can be set
    sleep(1);
    //We set the seed for the random numbers based of the pid
    srand(time(NULL) ^ (getpid() << 16));
    printf("sig generator process made\n");
    //This will determine our random nanosleep time
    struct timespec time1, time2;
    time1.tv_sec = 0;
    FILE *fp;
    time_t send_time;
    while (1)
    {
        
            
        //Add a random choice between sig 1 or sig 2
        int choice = (rand() % 2);
        //If its 1
        if (choice == 1)
        {
            //lok the sig1_send counter
            pthread_mutex_lock(&sig1_send);
            //Send the signal
            kill(0, SIGUSR1);
            //Open the sender log file
            fp = fopen("project4_sender_log.txt", "a");
            sig1_send_count->value++;
            time(&send_time);
            //then just log that it was sent in the file
            fprintf(fp, "SIGUSR1 Number %d Sent at: %s\n", sig1_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            //Then unlock the counter
            pthread_mutex_unlock(&sig1_send);

        }
        else
        {
            //Lock the SIGUSR2 counter
            pthread_mutex_lock(&sig2_send);
            //Send the signal
            kill(0, SIGUSR2);
            //Log the sent signal
            fp = fopen("project4_sender_log.txt", "a");
            sig2_send_count->value++;
            time(&send_time);
            fprintf(fp, "SIGUSR2 Number %d Sent at: %s\n", sig2_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            //Unlock the counter
            pthread_mutex_unlock(&sig2_send);
            
        }
        //Here we determine the amount of time we wait between signals
        time1.tv_nsec = ((rand() % 91) + 10) * 1000000;
        nanosleep(&time1, &time2);

    }
}
//This process will launch all of our seperate threads
void sig_handle_process()
{
    printf("sig handle process made\n");
    //Set the id to 1
    pthread_t thread_id = 1;

    //Then create two SIGUSR1 catcher threads
    pthread_create(&thread_id, NULL, sig1_thread, NULL);
    thread_id++;
    pthread_create(&thread_id, NULL, sig1_thread, NULL);
    thread_id++;
    //Then create two SIGUSR2 catcher threads
    pthread_create(&thread_id, NULL, sig2_thread, NULL);
    thread_id++;
    pthread_create(&thread_id, NULL, sig2_thread, NULL);
    thread_id++;
    //Then the original thread becomes our reporting thread
    report_thread();
}
//The meat of this is similar to the signal generator process so I will skip commenting the same parts
void parent_process(int send1, int send2, char *timer)
{
    printf("parent process going\n");
    //Set up the values
    struct shared_val *sig1_send_count = (struct shared_val *)shmat(send1, NULL, 0);
    struct shared_val *sig2_send_count = (struct shared_val *)shmat(send2, NULL, 0);
    pthread_mutex_t sig1_send = sig1_send_count->mutex;
    pthread_mutex_t sig2_send = sig2_send_count->mutex;
    sleep(1);
    srand(time(NULL) ^ (getpid() << 16));
    struct timespec time1, time2;
    time1.tv_sec = 0;
    FILE *fp;
    time_t send_time;
    //The only thing different is the parent thread will stop looping based on one of two conditions
    //1 = Do 10000 signals then stop
    
    if (strcmp(timer, "1")==0)
    {//1 = Do 10000 signals then stop
        while ((sig1_send_count->value+sig2_send_count->value)<=10000)
        {
            
        //Add a random choice between sig 1 or sig 2
        int choice = (rand() % 2);

        if (choice == 1)
        {
            
            pthread_mutex_lock(&sig1_send);
            kill(0, SIGUSR1);
            fp = fopen("project4_sender_log.txt", "a");
            sig1_send_count->value++;
            time(&send_time);
            fprintf(fp, "SIGUSR1 Number %d Sent at: %s\n", sig1_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            pthread_mutex_unlock(&sig1_send);

            //raise(SIGUSR1);
            //printf("sig1 sent\n");
        }
        else
        {
            
            pthread_mutex_lock(&sig2_send);
            kill(0, SIGUSR2);
            fp = fopen("project4_sender_log.txt", "a");
            sig2_send_count->value++;
            time(&send_time);
            fprintf(fp, "SIGUSR2 Number %d Sent at: %s\n", sig2_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            pthread_mutex_unlock(&sig2_send);
            //raise(SIGUSR2);
            //printf("sig2 sent\n");
        }
        
        time1.tv_nsec = ((rand() % 91) + 10) * 1000000;
        nanosleep(&time1, &time2);
        }

       
    } else{
        time_t end;
        //2 = Run the program for 30 seconds
        while ((end-start)<=30)
        {
            time(&end);
        //Add a random choice between sig 1 or sig 2
        int choice = (rand() % 2);

        if (choice == 1)
        {
            
            pthread_mutex_lock(&sig1_send);
            kill(0, SIGUSR1);
            fp = fopen("project4_sender_log.txt", "a");
            sig1_send_count->value++;
            time(&send_time);
            fprintf(fp, "SIGUSR1 Number %d Sent at: %s\n", sig1_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            pthread_mutex_unlock(&sig1_send);

            //raise(SIGUSR1);
            //printf("sig1 sent\n");
        }
        else
        {
            
            pthread_mutex_lock(&sig2_send);
            kill(0, SIGUSR2);
            fp = fopen("project4_sender_log.txt", "a");
            sig2_send_count->value++;
            time(&send_time);
            fprintf(fp, "SIGUSR2 Number %d Sent at: %s\n", sig2_send_count->value, asctime(localtime(&send_time)));
            fclose(fp);
            pthread_mutex_unlock(&sig2_send);
            //raise(SIGUSR2);
            //printf("sig2 sent\n");
        }
        
        time1.tv_nsec = ((rand() % 91) + 10) * 1000000;
        nanosleep(&time1, &time2);
    
        }
    }
    //Then we kill all the programs when the timer/counter is done
    shmdt(sig1_send_count);
    shmdt(sig2_send_count);
     kill(0, SIGKILL);
     kill(0, SIGKILL);
     kill(0, SIGKILL);
     kill(0, SIGKILL);
     kill(0, SIGKILL);

    exit(0);
}

//This is the signal mask that blocks SIGUSR1
void block_sigusr1()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR1);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
}
//This is the signal mask that blocks SIGUSR2
void block_sigusr2()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGUSR2);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
}

//This handles our SIGUSR1 signals and adds an entry to the log array
void siguser1_handler(int num)
{
    //Lock the recieve counter
    pthread_mutex_lock(&sig1_recieve);
    //increment it
    sig1_recieve_count++;
    //Lock the log array
    pthread_mutex_lock(&log_array_lock);
    //Then we set up a temp entry
    Entry temp;
    time_t now;
    time(&now);
    temp.arrivalTime = now;
    temp.num = sig1_recieve_count;
    //This gets the thread id
    temp.id = syscall(SYS_gettid);
    temp.type = 1;
    //Then adds temp to the array
    addEntry(log_array, temp, 99);
    //Then unlocks everything
    pthread_mutex_unlock(&log_array_lock);
    pthread_mutex_unlock(&sig1_recieve);
}

//Our sigusr2 handler
void siguser2_handler(int num)
{//Same system as siguser1_handler so I will not comment this part
    pthread_mutex_lock(&sig2_recieve);
    sig2_recieve_count++;
    pthread_mutex_lock(&log_array_lock);
    Entry temp;
    time_t now;
    time(&now);
    temp.arrivalTime = now;
    temp.num = sig2_recieve_count;
    temp.id = syscall(SYS_gettid);
    temp.type = 2;
    addEntry(log_array, temp, 99);
    pthread_mutex_unlock(&log_array_lock);
    pthread_mutex_unlock(&sig2_recieve);
}

//Removes an entry from the array
void removeEntry(Entry *array, int index, int array_length)
{
    int i;
    for (i = index; i < array_length - 1; i++)
    {

        array[i] = array[i + 1];
    }
}
//Adds an entry to the array
void addEntry(Entry *array, Entry newJob, int array_length)
{
    int i;
    for (i = 0; i < array_length - 1; i++)
    {
        if (!(array[i].type > 0))
        {

            array[i] = newJob;

            return;
        }
    }
}
//This just checks the size of the entry array
int checkSize(Entry array[], int size)
{
    int i = 0, j = 0;
    for (i = 0; i < size; i++)
    {
        if (array[i].type > 0)
        {
            j++;
        }
    }
    return j;
}
