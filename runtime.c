/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  char *command;
  int jobNumber;
  pid_t pid;
  char *status;
  struct bgjob_l* next;
} bgJobL;

/* List of the background processes */
bgJobL *bgJobsHead = NULL;
bgJobL *bgJobsTail = NULL;

// The job in the foreground
bgJobL *fgJob = NULL;

//Boolean value used to indicate if there is a forground process we're waiting on
bool waiting = FALSE;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);
/* Adds new background job to the list of background jobs */
static void AddBgJobToList(pid_t jobId, char* command);
/* Removes an existing background job from the list of background jobs */
static void RemoveBgJobFromList(pid_t jobId);
/* Print the list of background jobs (bgJobsHead) */
static void PrintBgJobList();
/* Print a particular background job */
static void printBgJob(pid_t jobPid);
/* Catch signials from child processes and reap zombie processes */
static void sigchld_handler();
/* Return a backgroun job to the  and notify the user */
static void bringToForeground(int jobId);
/* Send sigcont signal to background job */
static void continueBgJob(int jobNumber);
/* Create a new bgJobL struct */
static bgJobL* createBgJobL();
/* Release and collect the space of a bgJobL struct */
static void releaseBgJobL(bgJobL **jobToDelete);
/* Change the status of an existing job */
static void changeBgJobStatus(pid_t jobId, char* status);
/* Wait for foreground process to finish */
static void waitFg();
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

void RunCmdBg(commandT* cmd)
{
  // TODO
}

void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
  //Initialize the SIGCHLD catcher
  signal (SIGCHLD, sigchld_handler);

  //Block sigchld until job is added to the bgjob list or recorded in fgJob
  sigset_t x;
  sigemptyset (&x);
  sigaddset(&x, SIGCHLD);
  sigprocmask(SIG_BLOCK, &x, NULL);

  //Create a copy of the current state
  pid_t childPid = fork();

  //If there was an error when creating the child process
  if (childPid == -1)
  {
    fprintf(stderr, "%s\n", "There was a fork error when executing your command");
    fflush(stdout);
    exit(1);
  }
  //If the process that is running is the child, execute the comand
  else if (childPid == 0)
  {
    //Change the process group ID of the child to stop signals from affecting tsh
    setpgid(0,0);
    //Unblock sigchld signal
    sigprocmask(SIG_UNBLOCK, &x, NULL);
    //Execute the program
    execv(cmd->name,cmd->argv);
    //Notify user if there is an error (won't be called if execv works)
    fprintf(stderr, "%s\n", "command not found");
    fflush(stdout);
    exit(0);
  }
  //If the process that is running is the parent...
  else
  {
    //If the command is for a background job (bg in command is set to 1)...
    if (cmd->bg == 1)
    {
      //Add the job to the background job list (bgJobsHead)
      AddBgJobToList(childPid, cmd->cmdline);
      //Unblock sigchld so child process can be reaped when completed
      sigprocmask(SIG_UNBLOCK, &x, NULL);
      //Do NOT tell the parent process to wait
    }
    //If the command is NOT for a background job (bg in command is set to 0)...
    else
    {
      //Record the job information in a bgJobL object in case it is interupted
      fgJob = createBgJobL();
      fgJob->command = malloc(strlen(cmd->cmdline)+1);
      strncpy(fgJob->command, cmd->cmdline, strlen(cmd->cmdline)+1);
      fgJob->pid = childPid;
      //Unblock sigchld so child process can be reaped when completed
      sigprocmask(SIG_UNBLOCK, &x, NULL);
      //wait for the child to finish
      waiting = TRUE;
      waitFg();
      //waiting variable set to false and fgJob is freed in sigchld_handler()
    }
  }
}

//Test to see if command needs to be executed by the shell itself
static bool IsBuiltIn(char* cmd)
{
  //If the command is any of these things, it's a built in command (return true)
  if (strncmp(cmd, "bg", 2) == 0)
    return TRUE;

  else if (strncmp(cmd, "fg", 2) == 0) 
    return TRUE;

  else if (strncmp(cmd, "jobs", 4) == 0)
    return TRUE;
  else if (strncmp(cmd, "cd", 2) == 0)
    return TRUE;
  //Otherwise it isn't (return false)
  else
    return FALSE;
}

//Run commands that are built-in shell functions
static void RunBuiltInCmd(commandT* cmd)
{
  //Send SIGCONT to a backgrounded job, but do not give it the foreground 
  if (strncmp(cmd->argv[0], "bg", 2) == 0)
  {
    //If there are two arguments in the command...
    if (cmd->argc == 2)
      //Bring the process with the given jobNumber
      continueBgJob(strtol(cmd->argv[1],NULL,10));
    //If there is one argument in the command...
    else if (cmd->argc == 1)
      //Bring the most recent background process to the foreground
      continueBgJob(-1);
    else
    {
      fprintf(stderr, "Too many arguments were given with bg.\n");
    }
  }
  //Return a backgrounded job to the foreground 
  else if (strncmp(cmd->argv[0], "fg", 2) == 0)
  {
    //If there are two arguments in the command...
    if (cmd->argc == 2)
      //Bring the process with the given jobNumber
      bringToForeground(strtol(cmd->argv[1],NULL,10));
    //If there is one argument in the command...
    else if (cmd->argc == 1)
      //Bring the most recent background process to the foreground
      bringToForeground(-1);
    else
    {
      fprintf(stderr, "Too many arguments were given with fg.\n");
    }
  }
  //Print the list of background jobs (bgJobsHead)
  else if (strncmp(cmd->argv[0], "jobs", 4) == 0)
    PrintBgJobList();
  
  else
  {
    fprintf(stderr, "%s is an unrecognized internal command\n", cmd->argv[0]);
    fflush(stdout);
  }
}

//Wait for a foreground process to terminate
static void waitFg()
{
  //Waiting will be set to false once foreground process terminates
  while(waiting)
  {
    //Amount of time doesn't matter as long as it isn't tiny
    sleep(1);
  }
}

// Catch signials from child processes and reap zombie processes
static void sigchld_handler()
{
  //Initialize variables
  pid_t childPid;
  int status = 0;
  //Check the status of all jobs and clean up jobs that are finished (waitpid does the cleaning)
   while ((childPid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
  {
    //If the job has 
    //finished normally, finished due to being signaled, or stopped due to being signaled...
    if (WIFEXITED(status) || WIFSIGNALED(status) || WIFSTOPPED(status) )
    {
      //If the job is a foreground job
      if(fgJob != NULL && fgJob->pid == childPid)
      {
        //Set waiting to false to escape loop in waitFg()
        waiting = FALSE;
        //Free the bgJobL object associated with foreground processes
        if(fgJob != NULL) releaseBgJobL(&fgJob);
      }
      //If the job is a background job
      else
      {
        //Change job's status to done
        changeBgJobStatus(childPid, "Done\0");
      }
    }
  }
}
//ctrl-z signal handler (stops a foreground process if any)
void stopFgProc()
{
  //If there is a foreground process...
  if (fgJob != NULL)
  {
    //Add it to the background job list
    AddBgJobToList(fgJob->pid, fgJob->command);
    //Change it's status in the job list to "stopped"
    changeBgJobStatus(fgJob->pid, "Stopped\0");
    //Notify user that the job has been stopped
    printBgJob(fgJob->pid);
    //Stop it and all of its children
    kill(-(fgJob->pid), SIGSTOP);
  } 
}
//ctrl-c signal handler (kills a foreground process if any)
void killFgProc()
{
  //If tehre is a foreground process...
  if (fgJob != NULL)
  {
    //Kill it and all of its children
    kill(-(fgJob->pid), SIGINT);
  } 
}

//Notifies user of jobs that were completed and cleans background job list
void CheckJobs()
{
  //Initialize variables
  bgJobL *job = bgJobsHead; //This is the leading pointer
  bgJobL *prevJob = NULL; //This is the trailing pointer (one node behind leading)
  bgJobL *jobToDel = NULL; //Job pointer for deletion
  //While we aren't at the end of the list (and the list still has nodes)
  while (job != NULL)
  {
    if (strncmp(job->status, "Done\0", 5) == 0)
    {
      //Print notification that the job was completed
      fprintf(stdout, "[%d]   %s                    %s\n",job->jobNumber, job->status, job->command);
      fflush(stdout);
      
      //If the job to be deleted is the tail of the linked list...
        if (job == bgJobsTail)
          //Set the tail to the job before the one to be deleted
          bgJobsTail = prevJob;
        //If the job to be deleted is the head of the linked list...
        if (job == bgJobsHead)
          //Make the head of the linked list point to the next node
          bgJobsHead = job->next;

        //If the job to be deleted is in the middle or at the end of the linked list...
        else
          //Remove the job to be delted from the list by making the node that points to it point to
          //the node after it
          prevJob->next = job->next;

        //Move to the next node
        jobToDel = job;
        job = job->next;
        //deallocate the memory the job node was using
        releaseBgJobL(&jobToDel);
    }
    else
    {
      //set the traling pointer to the leading pointer
      prevJob = job;
      //set the leading pointer to the next node
      job = job->next;
    }
  }
}

//Kills all background processes if any before exiting
void cleanExit()
{
  //Initialize variables
  bgJobL *bgJob = bgJobsHead;
  bgJobL *jobToDel = NULL;
  //Iterate through linked list, kill every background job, and free every node
  while (bgJob != NULL)
  {
    kill(-(bgJob->pid), SIGINT);
    jobToDel = bgJob;
    bgJob = bgJob->next;
    releaseBgJobL(&jobToDel);
  }
  bgJobsHead = NULL;
  bgJobsTail = NULL;
}

//Send sigcont signal to background job
static void continueBgJob(int jobNumber)
{
  //If the background job list isn't empty...
  if(bgJobsHead)
  {
    //Block sigchld until job has been added to fgJob and removed from background job list
    sigset_t x;
    sigemptyset (&x);
    sigaddset(&x, SIGCHLD);
    sigprocmask(SIG_BLOCK, &x, NULL);
    //If no job number was given, default to the most recently backgrounded job
    if (jobNumber == -1)
      jobNumber = bgJobsTail->jobNumber;
    //Initialize variables
    bgJobL *bgJob = bgJobsHead;
    //Iterate through linked list of background jobs
    while (bgJob != NULL)
    {
      //If the bgJob is the job you're looking for...
      if (bgJob->jobNumber == jobNumber)
      {
        //Tell job to continue working if it has been stopped
        kill(bgJob->pid,SIGCONT);
        //Change it's status in the job list to "stopped"
        changeBgJobStatus(bgJob->pid, "Running\0");
        break;
      }
      bgJob = bgJob->next;
    }
    sigprocmask(SIG_UNBLOCK, &x, NULL);
  }
}

//Return a backgroun job to the foreground and notify the user
static void bringToForeground(int jobNumber)
{
  //If the background job list isn't empty...
  if(bgJobsHead)
  {
    //If no job number was given, default to the most recently backgrounded job
    if (jobNumber == -1)
      jobNumber = bgJobsTail->jobNumber;
    //Initialize variables
    bgJobL *bgJob = bgJobsHead;
    //Iterate through linked list of background jobs
    while (bgJob != NULL)
    {
      //If the bgJob is the job you're looking for...
      if (bgJob->jobNumber == jobNumber)
      {
        //Block sigchld until job has been added to fgJob and removed from background job list
        sigset_t x;
        sigemptyset (&x);
        sigaddset(&x, SIGCHLD);
        sigprocmask(SIG_BLOCK, &x, NULL);
        //If the job is currently stopeed...
        if(strncmp(bgJob->status, "Stopped\0", 8) == 0)
          //Tell job to continue working
          kill(bgJob->pid,SIGCONT);
        //Record the job information in a bgJobL object in case it is interupted
        fgJob = createBgJobL();
        fgJob->command = malloc(strlen(bgJob->command)+1);
        strncpy(fgJob->command, bgJob->command, strlen(bgJob->command)+1);
        fgJob->pid = bgJob->pid;
        //Remove the status of the job so nothing prints when removing the job from the background job list
        if((bgJob)->status != NULL)
        {
          free(bgJob->status);
          bgJob->status = NULL;
        }
        //Remove the job from the background job list
        RemoveBgJobFromList(bgJob->pid);
        //wait for the job to finish
        waiting = TRUE;
        //Unblock the sigchld
        sigprocmask(SIG_UNBLOCK, &x, NULL);
        waitFg();
        //waiting variable set to false and fgJob is freed in sigchld_handler()
        //Exit the loop
        break;
      }
      //If bgJob isn't the job you're looking for, move to the next job in the list
      bgJob = bgJob->next;
    }
  }
}

//Print a particular background job
static void printBgJob(pid_t jobPid)
{
  //Initialize variables
  bgJobL *bgJob = bgJobsHead;
  //Iterate through linked list and print the job number, status, and command in a particular node
  while (bgJob != NULL)
  {
    //If the job matches the job we're looking for...
    if (bgJob->pid == jobPid)
    {
      //If the process is stopeed...
      if (strncmp(bgJob->status, "Stopped\0", 8) == 0)
        //Print inforamatino without an "&" symbol
        fprintf(stdout, "[%d]   %s                 %s\n", bgJob->jobNumber,bgJob->status, bgJob->command);
      //If the process is running...
      else if (strncmp(bgJob->status, "Running\0", 8) == 0)
        //Print inforamatino with an "&" symbol
        fprintf(stdout, "[%d]   %s                 %s&\n", bgJob->jobNumber,bgJob->status, bgJob->command);
      //Print the thing immediately
      fflush(stdout);
      //Exit the loop
      break;
    }
    //If not, check the next node
    bgJob = bgJob->next;
  }
}

//Print the list of background jobs (bgJobsHead)
static void PrintBgJobList()
{
  //Initialize variables
  bgJobL *bgJob = bgJobsHead;
  //Iterate through linked list and print the job number, status, and command in every node
  while (bgJob != NULL)
  {
    if (strncmp(bgJob->status, "Stopped\0", 8) == 0)
      fprintf(stdout, "[%d]   %s                 %s\n", bgJob->jobNumber,bgJob->status, bgJob->command);
    else if (strncmp(bgJob->status, "Running\0", 8) == 0)
      fprintf(stdout, "[%d]   %s                 %s&\n", bgJob->jobNumber,bgJob->status, bgJob->command);
    fflush(stdout);
    bgJob = bgJob->next;
  }
}
//Add new background job to the end of the background jobs list (bgJobsTail)
static void AddBgJobToList(pid_t jobId, char* command)
{
  //Allocate memory for the new background job
  bgJobL *newJob = createBgJobL();

  //Fill in PID for new background job
  newJob->pid = jobId;
  //Fill command text for new background job
  newJob->command = malloc(strlen(command)+1);
  strncpy(newJob->command, command, strlen(command)+1);
  //Fill status text for new background job
  newJob->status = malloc(strlen("Running\0")+1);
  strncpy(newJob->status, "Running\0",strlen("Running\0")+1);
  //Fill in the job number for the new background job
  if (bgJobsTail !=  NULL)
    //Job number = one more than the last job number
    newJob->jobNumber = bgJobsTail->jobNumber + 1;
  else
    //If no previous jobs in the list, job number is 1
    newJob->jobNumber = 1;
  //Fill in the "next" node for the new job (will always be null)
  newJob->next = NULL;

  //If the list has at least one node (and there fore a tail), have the tail point to the new node
  if(bgJobsTail != NULL)
    bgJobsTail->next = newJob;
  //If the list is empty, make the new job the head of the background jobs list
  else
    bgJobsHead = newJob;

  //Make the new job the tail of the background jobs list
  bgJobsTail = newJob;

}

// Removes an existing background job from the list of background jobs
static void RemoveBgJobFromList(pid_t jobId)
{
  //Initialize variables to iterate through the list of background jobs
  bgJobL *job = bgJobsHead; //This is the leading pointer
  bgJobL *prevJob = NULL; //This is the trailing pointer (one node behind leading)

  //Iterate through the job list until you reach the end or until the job to be deleted is found
  while (job != NULL)
    {
      //If the job to be deleted is found...
      if (job->pid == jobId)
      {
        //If the job to be deleted is the tail of the linked list...
        if (job == bgJobsTail)
          //Set the tail to the job before the one to be deleted
          bgJobsTail = prevJob;
        //If the job to be deleted is the head of the linked list...
        if (job == bgJobsHead)
          //Make the head of the linked list point to the next node
          bgJobsHead = job->next;

        //If the job to be deleted is in the middle or at the end of the linked list...
        else
          //Remove the job to be delted from the list by making the node that points to it point to
          //the node after it
          prevJob->next = job->next;
        //deallocate the memory the job node was using
        releaseBgJobL(&job);
        //Leave the while loop
        break;
      }
      //If the job to be deleted wasn't found (yet)...
      else
      {
        //set the traling pointer to the leading pointer
        prevJob = job;
        //set the leading pointer to the next node
        job = job->next;
      }
    }
    //If the node to be deleted isn't found, do nothing
}


//////////////////////////////////////////////////////////////
//  CmdT Functions
//////////////////////////////////////////////////////////////

commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}

//////////////////////////////////////////////////////////////
//  bgJobL Functions
//////////////////////////////////////////////////////////////

//Create a new bgJobL struct
static bgJobL* createBgJobL()
{
  bgJobL *newJob = malloc(sizeof(bgJobL));
  newJob->command = NULL;
  newJob->status = NULL;
  return newJob;
}
//Release and collect the space of a bgJobL struct
static void releaseBgJobL(bgJobL **jobToDelete)
{
  if((*jobToDelete)->command != NULL) free((*jobToDelete)->command);
  if((*jobToDelete)->status != NULL) free((*jobToDelete)->status);
  free(*jobToDelete);
  *jobToDelete = NULL;
}

//Change the status of an existing job
static void changeBgJobStatus(pid_t jobId, char* status)
{
  //Initialize variables
  bgJobL *bgJob = bgJobsHead;
  //Iterate through linked list of background jobs
  while (bgJob != NULL)
  {
    //If the current background job matches the provided process ID...
    if (bgJob->pid == jobId)
    {
      //Remove the current status
      if((bgJob)->status != NULL) free((bgJob)->status);
      //Make space for the new status
      bgJob->status = malloc(strlen(status)+1);
      //Record the new status
      strncpy(bgJob->status, status, strlen(status)+1);
      //Exit the loop
      break;
    }
    //Get the next job from the list
    bgJob = bgJob->next;
  }
}
