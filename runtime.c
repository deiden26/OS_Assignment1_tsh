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
  pid_t pid;
  struct bgjob_l* next;
} bgjobL;

/* the pids of the background processes */
 bgjobL *bgjobs = NULL;

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
static void AddBgJobToList(pid_t jobId);
/* Removes an existing background job from the list of background jobs */
static void RemoveBgJobFromList(pid_t jobId);
/* Print the list of background jobs (bgJobs) */
static void PrintBgJobList();
/* Catch signials from child processes and reap zombie processes */
static void sigchld_handler();
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
  //Create a copy of the current state
  pid_t childPid = fork();

  //If there was an error when creating the child process
  if (childPid == -1)
  {
    fprintf(stderr, "%s\n", "There was a fork error when executing your command");
    exit(1);
  }
  //If the process that is running is the child, execute the comand
  else if (childPid == 0)
  {
    execv(cmd->name,cmd->argv);
    fprintf(stderr, "%s\n", "command not found");
    exit(0);
  }
  //If the process that is running is the parent...
  else
  {
    //If the command is for a background job (bg in command is set to 1)...
    if (cmd->bg == 1)
    {
      //Print notification of the process being run in the background
      fprintf(stdout, "Process:%s PID:%d running in background\n", cmd->argv[0],childPid);
      //Add the job to the background job list (bgjobs)
      AddBgJobToList(childPid);
      //Do NOT tell the parent process to wait
    }
    //If the command is NOT for a background job (bg in command is set to 0)...
    else
      //wait for the child to finish
      waitpid(childPid,0,0);
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
  //Otherwise it isn't (return false)
  else
    return FALSE;
}

//Run commands that are built-in shell functions
static void RunBuiltInCmd(commandT* cmd)
{
  //Send SIGCONT to a backgrounded job, but do not give it the foreground 
  if (strncmp(cmd->argv[0], "bg", 2) == 0)
    fprintf(stderr, "%s is an unrecognized internal command\n", cmd->argv[0]);
  //Return a backgrounded job to the foreground 
  else if (strncmp(cmd->argv[0], "fg", 2) == 0) 
    fprintf(stderr, "%s is an unrecognized internal command\n", cmd->argv[0]);
  //Print the list of background jobs (bgJobs)
  else if (strncmp(cmd->argv[0], "jobs", 4) == 0)
    PrintBgJobList();
  
  else
    fprintf(stderr, "%s is an unrecognized internal command\n", cmd->argv[0]);
}

// Catch signials from child processes and reap zombie processes
static void sigchld_handler()
{
  //Initialize variables
  pid_t childPid;
  int status = 0;
  //Check the status of all background jobs and clean up jobs that are finished (waitpid does the cleaning)
   while ((childPid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    //If the job is finished...
    if (WIFEXITED(status) || WIFSIGNALED(status))
    {
      //Remove the finished job
      RemoveBgJobFromList(childPid);
    }
  }
}

void CheckJobs()
{
}

//Print the list of background jobs (bgJobs)
static void PrintBgJobList()
{
  //Test for there being no background jobs
  if (bgjobs == NULL)
    fprintf(stdout, "There are no background jobs\n");
  //If there are background jobs, print them in a list
  else
  {
    //Print the top the the table
    fprintf(stdout, "Background jobs in order of recency:\n");
    fprintf(stdout, "|%-10s|%-10s|\n","Order","PID");
    fprintf(stdout, "|----------|----------|\n");
    //Initialize variables
    bgjobL *bgJob = bgjobs;
    int counter = 1;
    //Iterate through linked list and print PID in every node
    while (bgJob != NULL)
    {
      fprintf(stdout, "|%-10d|%-10d|\n", counter, bgJob->pid);
      bgJob = bgJob->next;
      counter++;
    }
  }
}
//Add new background job to the front of the background jobs list (bgJobs)
static void AddBgJobToList(pid_t jobId)
{
  //Allocate memory for the new background job
  bgjobL *newJob = malloc(sizeof(bgjobL));
  //Fill in PID for new background job
  newJob->pid = jobId;
  //Make new background job point to the head of the background jobs list
  newJob->next = bgjobs;
  //Make the new job the head of the background jobs list
  bgjobs = newJob;
}
// Removes an existing background job from the list of background jobs
static void RemoveBgJobFromList(pid_t jobId)
{
  //Initialize variables to iterate through the list of background jobs
  bgjobL *job = bgjobs; //This is the leading pointer
  bgjobL *prevJob = NULL; //This is the trailing pointer (one node behind leading)

  //Iterate through the job list until you reach the end or until the job to be deleted is found
  while (job != NULL)
    {
      //If the job to be deleted is found...
      if (job->pid == jobId)
      {
        //If the job to be deleted is the head of the linked list...
        if (job == bgjobs)
          //Make the head of the linked list point to the next node
          bgjobs = job->next;

        //If the job to be deleted is in the middle or at the end of the linked list...
        else
          //Remove the job to be delted from the list by making the node that points to it point to
          //the node after it
          prevJob->next = job->next;

        //deallocate the memory the job node was using
        free(job);
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
