//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//  Cred: Claude 

#include "Scheduler.hpp"
#include <bits/stdc++.h>
using namespace std;

static bool migrating = false;

struct MachineInformation {
    MachineId_t machine_id;
    vector<VMId_t> vms; 
    Time_t last_referenced;
    double ready_time;  // For LBIMM scheduling
};

struct TaskScheduleInfo {
    TaskId_t task_id;
    MachineId_t machine_id;
    VMId_t vm_id;
    double execution_time;
    double completion_time;
};

static vector<MachineInformation> cluster;
static unordered_map<MachineId_t, bool> ready; 
static unordered_map<MachineId_t, vector<TaskId_t>> pending_tasks;
static vector<TaskId_t> task_batch;
static Time_t last_batch_time = 0;
static const Time_t BATCH_WINDOW = 50000; // 50ms batching window

double pendingExecutionTime(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    double total_instructions = 0.0;
    
    for(VMId_t vm_id : cluster[machine_id].vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        for(TaskId_t t_id : vm_info.active_tasks) {
            TaskInfo_t t_info = GetTaskInfo(t_id);
            total_instructions += (double)t_info.remaining_instructions;
        }
    }

    double total_mips = (double)machine_info.performance[machine_info.p_state] * machine_info.num_cpus;
    if (total_mips == 0) return DBL_MAX;
    return total_instructions / (total_mips * 1e6);
}

bool canScheduleTask(TaskId_t task_id, MachineId_t machine_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);

    if((machine_info.memory_used + task_info.required_memory) > machine_info.memory_size) {
        return false; 
    }

    double PET = pendingExecutionTime(machine_info.machine_id);
    double target_time_sec = max(1e-6, (double)(task_info.target_completion - task_info.arrival) / 1e6);
    double total_mips = (double)machine_info.performance[machine_info.p_state] * machine_info.num_cpus;
    double task_execution_time = (double)task_info.total_instructions / (total_mips *1e6);

    switch(task_info.required_sla) {
        case SLA0:
            return task_execution_time + PET + 10 <= target_time_sec;
        case SLA1:
            return task_execution_time + PET + 5 <= target_time_sec;
        case SLA2:
            return task_execution_time + PET + 1 <= target_time_sec;
        case SLA3:
            return task_execution_time + PET <= target_time_sec;
    }
    return false;
}

void printMachineInfo(MachineId_t machine_id) {
    MachineInfo_t m = Machine_GetInfo(machine_id);
    cout << "\n==============================\n";
    cout << "📘 Machine Information\n";
    cout << "------------------------------\n";
    cout << "Machine ID       : " << m.machine_id << endl;
    cout << "State (S-state)  : " << m.s_state << "  (0=S0 active, 5=S5 off)" << endl;
    cout << "CPU Type         : ";
    switch (m.cpu) {
        case ARM:   cout << "ARM"; break;
        case POWER: cout << "POWER"; break;
        case RISCV: cout << "RISC-V"; break;
        case X86:   cout << "x86"; break;
        default:    cout << "Unknown"; break;
    }
    cout << endl;
    cout << "CPU Performance  : P" << m.p_state << endl;
    cout << "Number of CPUs   : " << m.num_cpus << endl;
    cout << "GPU Present?     : " << (m.gpus ? "Yes" : "No") << endl;
    cout << "Memory Usage     : " << m.memory_used << " / " << m.memory_size << " MB" << endl;
    cout << "Active Tasks     : " << m.active_tasks << endl;
    cout << "Active VMs       : " << m.active_vms << endl;
    cout << "Energy Consumed  : " << fixed << setprecision(4)
         << (double)m.energy_consumed / 1000.0 << " kWh" << endl;
    cout << "==============================\n\n";
}

bool IsVMCompatibleWithCPU(VMType_t vm_type, CPUType_t cpu_type) {
    switch(vm_type) {
        case LINUX:
        case LINUX_RT:
            return true;
        case WIN:
            return (cpu_type == ARM || cpu_type == X86);
        case AIX:
            return (cpu_type == POWER);
        default:
            return false;
    }
}

VMId_t isTaskCompatible(TaskId_t task_id, MachineInfo_t machine_info) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    if(task_info.required_cpu != machine_info.cpu) {
        return UINT_MAX; 
    }

    for(VMId_t vm_id : cluster[machine_info.machine_id].vms) {
        VMInfo_t vm_info = VM_GetInfo(vm_id); 
        if(vm_info.vm_type == task_info.required_vm) {
            return vm_id; 
        }   
    }
    return UINT_MAX; 
}

double calculateExecutionTime(TaskId_t task_id, MachineId_t machine_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    
    if(task_info.required_cpu != machine_info.cpu) {
        return DBL_MAX;
    }
    
    double total_mips = (double)machine_info.performance[machine_info.p_state] * machine_info.num_cpus;
    if (total_mips == 0) return DBL_MAX;
    
    return (double)task_info.total_instructions / (total_mips * 1e6);
}

double calculateCompletionTime(TaskId_t task_id, MachineId_t machine_id) {
    return calculateExecutionTime(task_id, machine_id) + cluster[machine_id].ready_time;
}

// LBIMM Phase 1: Min-Min Algorithm
vector<TaskScheduleInfo> minMinSchedule(vector<TaskId_t>& task_set) {
    vector<TaskScheduleInfo> schedule;
    
    // Reset ready times for scheduling
    for(auto& machine : cluster) {
        machine.ready_time = pendingExecutionTime(machine.machine_id);
    }
    
    while(!task_set.empty()) {
        TaskId_t min_task = UINT_MAX;
        MachineId_t best_machine = UINT_MAX;
        double min_execution_time = DBL_MAX;
        double best_completion_time = DBL_MAX;
        
        // Step 1: For each task, find its minimum execution time across all machines
        for(TaskId_t task_id : task_set) {
            TaskInfo_t task_info = GetTaskInfo(task_id);
            double task_min_exec = DBL_MAX;
            MachineId_t task_best_machine = UINT_MAX;
            double task_best_completion = DBL_MAX;
            
            // Find machine with minimum completion time for this task
            for(auto& machine : cluster) {
                if(!ready[machine.machine_id]) continue;
                
                MachineInfo_t m_info = Machine_GetInfo(machine.machine_id);
                if(m_info.s_state != S0) continue;
                if(m_info.cpu != task_info.required_cpu) continue;
                
                double exec_time = calculateExecutionTime(task_id, machine.machine_id);
                double completion_time = exec_time + machine.ready_time;
                
                if(completion_time < task_best_completion) {
                    task_best_completion = completion_time;
                    task_best_machine = machine.machine_id;
                    task_min_exec = exec_time;
                }
            }
            
            // Step 2: Among all tasks, find the one with minimum execution time
            if(task_min_exec < min_execution_time) {
                min_execution_time = task_min_exec;
                min_task = task_id;
                best_machine = task_best_machine;
                best_completion_time = task_best_completion;
            }
        }
        
        if(min_task == UINT_MAX || best_machine == UINT_MAX) break;
        
        // Step 3: Assign the min execution time task to its best machine
        TaskScheduleInfo info;
        info.task_id = min_task;
        info.machine_id = best_machine;
        info.execution_time = min_execution_time;
        info.completion_time = best_completion_time;
        
        schedule.push_back(info);
        
        // Update machine ready time
        cluster[best_machine].ready_time = best_completion_time;
        
        // Remove scheduled task
        task_set.erase(remove(task_set.begin(), task_set.end(), min_task), task_set.end());
        
        SimOutput("Min-Min: Task " + to_string(min_task) + " -> Machine " + 
                 to_string(best_machine) + " (exec: " + to_string(min_execution_time) + "s)", 2);
    }
    
    return schedule;
}

// LBIMM Phase 2: Load Balancing
void loadBalanceSchedule(vector<TaskScheduleInfo>& schedule) {
    if(schedule.empty()) return;
    
    // Calculate makespan
    double makespan = 0;
    for(auto& machine : cluster) {
        makespan = max(makespan, machine.ready_time);
    }
    
    SimOutput("LBIMM Load Balance: Initial makespan = " + to_string(makespan) + "s", 2);
    
    bool improved = true;
    int iteration = 0;
    
    while(improved && iteration < 10) {
        improved = false;
        iteration++;
        
        // Find heaviest loaded machine
        MachineId_t heaviest_machine = UINT_MAX;
        double max_load = 0;
        
        for(auto& machine : cluster) {
            if(machine.ready_time > max_load) {
                max_load = machine.ready_time;
                heaviest_machine = machine.machine_id;
            }
        }
        
        if(heaviest_machine == UINT_MAX) break;
        
        // Find smallest task on heaviest machine
        TaskScheduleInfo* smallest_task = nullptr;
        double min_exec_time = DBL_MAX;
        
        for(auto& info : schedule) {
            if(info.machine_id == heaviest_machine && info.execution_time < min_exec_time) {
                min_exec_time = info.execution_time;
                smallest_task = &info;
            }
        }
        
        if(!smallest_task) break;
        
        // Try to find a better machine for this task
        MachineId_t new_machine = UINT_MAX;
        double new_completion = DBL_MAX;
        
        for(auto& machine : cluster) {
            if(machine.machine_id == heaviest_machine) continue;
            if(!ready[machine.machine_id]) continue;
            
            MachineInfo_t m_info = Machine_GetInfo(machine.machine_id);
            if(m_info.s_state != S0) continue;
            
            TaskInfo_t task_info = GetTaskInfo(smallest_task->task_id);
            if(m_info.cpu != task_info.required_cpu) continue;
            
            double exec_time = calculateExecutionTime(smallest_task->task_id, machine.machine_id);
            double completion = machine.ready_time + exec_time;
            
            // Only reschedule if new completion < makespan
            if(completion < makespan && completion < new_completion) {
                new_completion = completion;
                new_machine = machine.machine_id;
            }
        }
        
        // Perform rescheduling if improvement found
        if(new_machine != UINT_MAX) {
            SimOutput("LBIMM Rebalance: Task " + to_string(smallest_task->task_id) + 
                     " moved from Machine " + to_string(heaviest_machine) + 
                     " to Machine " + to_string(new_machine), 2);
            
            // Update old machine ready time
            cluster[heaviest_machine].ready_time -= smallest_task->execution_time;
            
            // Update task assignment
            smallest_task->machine_id = new_machine;
            smallest_task->execution_time = calculateExecutionTime(smallest_task->task_id, new_machine);
            smallest_task->completion_time = cluster[new_machine].ready_time + smallest_task->execution_time;
            
            // Update new machine ready time
            cluster[new_machine].ready_time = smallest_task->completion_time;
            
            // Recalculate makespan
            makespan = 0;
            for(auto& machine : cluster) {
                makespan = max(makespan, machine.ready_time);
            }
            
            improved = true;
            SimOutput("LBIMM Load Balance: New makespan = " + to_string(makespan) + "s", 2);
        } else {
            break;
        }
    }
}

void executeBatchSchedule(vector<TaskScheduleInfo>& schedule, Time_t now) {
    for(auto& info : schedule) {
        if(!ready[info.machine_id]) {
            pending_tasks[info.machine_id].push_back(info.task_id);
            continue;
        }
        
        MachineInfo_t machine_info = Machine_GetInfo(info.machine_id);
        if(machine_info.s_state != S0) {
            Machine_SetState(info.machine_id, S0);
            ready[info.machine_id] = false;
            pending_tasks[info.machine_id].push_back(info.task_id);
            continue;
        }
        
        VMId_t vm_id = isTaskCompatible(info.task_id, machine_info);
        TaskInfo_t task_info = GetTaskInfo(info.task_id);
        
        if(vm_id == UINT_MAX) {
            vm_id = VM_Create(task_info.required_vm, task_info.required_cpu);
            VM_Attach(vm_id, info.machine_id);
            cluster[info.machine_id].vms.push_back(vm_id);
        }
        
        cluster[info.machine_id].last_referenced = now;
        VM_AddTask(vm_id, info.task_id, MID_PRIORITY);
    }
}

void turnOffIdleMachines(Time_t now) {
    for(auto &machine : cluster) {
        MachineInfo_t machine_info = Machine_GetInfo(machine.machine_id); 
        bool shutDownMachine = true; 
        for(VMId_t vm_id : machine.vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id); 
            if(!vm_info.active_tasks.empty()) {
                shutDownMachine = false; 
                break; 
            }
        }
        
        if(shutDownMachine && (now - machine.last_referenced >= (uint64_t)600 * 1e6)) {
            if(machine_info.s_state != S5 && ready[machine.machine_id]) {
                SimOutput("Machine " + to_string(machine_info.machine_id) + " turned off", 1); 
                ready[machine.machine_id] = false; 
                Machine_SetState(machine.machine_id, S5); 
            }
        }
    }
}

void Scheduler::Init() {
    unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total machines = " + to_string(total), 2);
    cluster.resize(total);

    for(unsigned i = 0; i < total; i++) {
        MachineId_t machine_id = MachineId_t(i);
        cluster[i].machine_id = machine_id; 
        cluster[i].ready_time = 0.0;
        MachineInfo_t machine_info = Machine_GetInfo(cluster[i].machine_id);
        VMId_t vm_id = VM_Create(LINUX, machine_info.cpu);
        cluster[i].vms.push_back(vm_id);
        VM_Attach(vm_id, cluster[i].machine_id);
        ready[machine_id] = true; 
        cluster[i].last_referenced = 0; 
    }

    SimOutput("Scheduler::Init(): LBIMM scheduler initialized.", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // vm_ready[vm_id] = true;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Add task to batch
    task_batch.push_back(task_id);
    
    // Check if we should process the batch
    bool should_process = false;
    
    // Process batch if: window expired OR batch size >= 10 OR high priority task
    if(now - last_batch_time >= BATCH_WINDOW) {
        should_process = true;
    } else if(task_batch.size() >= 10) {
        should_process = true;
    } else {
        TaskInfo_t task_info = GetTaskInfo(task_id);
        if(task_info.required_sla == SLA0 || task_info.required_sla == SLA1) {
            should_process = true;
        }
    }
    
    if(!should_process) {
        return; // Wait for more tasks
    }
    
    SimOutput("LBIMM: Processing batch of " + to_string(task_batch.size()) + " tasks", 1);
    
    // LBIMM Phase 1: Min-Min scheduling
    vector<TaskId_t> tasks_copy = task_batch;
    vector<TaskScheduleInfo> schedule = minMinSchedule(tasks_copy);
    
    // LBIMM Phase 2: Load balancing
    loadBalanceSchedule(schedule);
    
    // Execute the schedule
    executeBatchSchedule(schedule, now);
    
    // Clear batch and update time
    task_batch.clear();
    last_batch_time = now;
    
    // Periodic idle machine shutdown
    static int call_count = 0;
    if(++call_count % 500 == 0) {
        turnOffIdleMachines(now);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // Process any remaining tasks in batch
    if(!task_batch.empty() && now - last_batch_time >= BATCH_WINDOW * 2) {
        SimOutput("PeriodicCheck: Processing remaining " + to_string(task_batch.size()) + " tasks", 1);
        
        vector<TaskId_t> tasks_copy = task_batch;
        vector<TaskScheduleInfo> schedule = minMinSchedule(tasks_copy);
        loadBalanceSchedule(schedule);
        executeBatchSchedule(schedule, now);
        
        task_batch.clear();
        last_batch_time = now;
    }
}

void Scheduler::Shutdown(Time_t time) {
    for(auto &machine: cluster) {
        for(VMId_t vm_id: machine.vms) {
            // VM_Shutdown(vm_id); 
        }
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 4);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    migrating = false; 
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
}

void SchedulerCheck(Time_t time) {
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {

}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " state change complete at " + to_string(time), 3);
    ready[machine_id] = true; 
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    SimOutput("Machine " + to_string(machine_id) + " changed state to " + to_string(machine_info.s_state), 1);
    if(machine_info.s_state == S0) {
        for(TaskId_t task_id : pending_tasks[machine_id]) {
            VMId_t vm_id = isTaskCompatible(task_id, machine_info);
            TaskInfo_t task_info = GetTaskInfo(task_id); 
            if(vm_id == UINT_MAX) {
                vm_id = VM_Create(task_info.required_vm, task_info.required_cpu);
                VM_Attach(vm_id, machine_id);
                cluster[machine_id].vms.push_back(vm_id);
            }
            VM_AddTask(vm_id, task_id, MID_PRIORITY);
        }
        pending_tasks.erase(machine_id); 
    }
}