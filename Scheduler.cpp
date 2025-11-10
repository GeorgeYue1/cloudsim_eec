//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <bits/stdc++.h>
using namespace std;

static bool migrating = false;

struct MachineInformation {
    MachineId_t machine_id;
    vector<VMId_t> vms; 
};

static vector<MachineInformation> cluster;

bool exceedsLoadFactor(TaskId_t task_id, MachineInfo_t machine_info) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    return (machine_info.memory_used + task_info.required_memory) > machine_info.memory_size;
}
// Helper function to check if VM type is compatible with CPU type
bool IsVMCompatibleWithCPU(VMType_t vm_type, CPUType_t cpu_type) {
    switch(vm_type) {
        case LINUX:
        case LINUX_RT:
            return true;  // Works with all CPU types
        case WIN:
            return (cpu_type == ARM || cpu_type == X86);
        case AIX:
            return (cpu_type == POWER);
        default:
            return false;
    }
}

/**
 * Returns VMId if there's a vm that's compatible with the task 
 * Otherwise, returns -1; 
 */
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

void Scheduler::Init() {
    unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total machines = " + to_string(total), 2);
    cluster.resize(total);

    for (unsigned i = 0; i < total; i++) {
        cluster[i].machine_id = MachineId_t(i);
        MachineInfo_t machine_info = Machine_GetInfo(cluster[i].machine_id);
        VMId_t vm_id = VM_Create(LINUX, machine_info.cpu);
        cluster[i].vms.push_back(vm_id);
        VM_Attach(vm_id, cluster[i].machine_id);
    }

    SimOutput("Scheduler::Init(): Greedy scheduler initialized.", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // vm_ready[vm_id] = true;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    vector<pair<uint64_t, MachineId_t>> machine_utilizations;
    for(auto& machine : cluster) {
        MachineInfo_t machine_info = Machine_GetInfo(machine.machine_id);
        machine_utilizations.push_back({machine_info.energy_consumed, machine.machine_id}); 
    }

    sort(machine_utilizations.begin(), machine_utilizations.end()); 

    for(auto& utilizations : machine_utilizations) {
        MachineInfo_t machine_info = Machine_GetInfo(utilizations.second);
        if(machine_info.s_state != S5 && !exceedsLoadFactor(task_id, machine_info)) {
            VMId_t vm_id = isTaskCompatible(task_id, machine_info); 
            if(vm_id != UINT_MAX) {
                VM_AddTask(vm_id, task_id, MID_PRIORITY); 
                break; 
            }
        }
    }
    
}

void Scheduler::PeriodicCheck(Time_t now) {
    // Periodic optimization can be added here if needed
    // for(auto& machine : cluster) {
    //     MachineInfo_t machine_info = Machine_GetInfo(machine.machine_id);
    //     if(machine_info.active_tasks == 0) {
    //         for(VMId_t vm_id : machine.vms) {
    //             VM_Shutdown(vm_id); 
    //         }
    //         Machine_SetState(machine.machine_id, S5); 
    //     }
    // }
}

void Scheduler::Shutdown(Time_t time) {
    for(auto &machine: cluster) {
        for(VMId_t vm_id: machine.vms) {
            VM_Shutdown(vm_id); 
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
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " state change complete at " + to_string(time), 3);
    
    // for(auto &machine: cluster) {
    //     if(machine.id == machine_id) {
    //         machine.active = true;
            
    //         VMInfo_t vm_info = VM_GetInfo(machine.vm_id);
    //         if(vm_info.machine_id != machine.id) {
    //             VM_Attach(machine.vm_id, machine.id);
    //             vm_info = VM_GetInfo(machine.vm_id);
    //         }
    //         vm_ready[machine.vm_id] = (vm_info.machine_id == machine.id);
            
    //         // Process pending tasks
    //         while(!pending_tasks[machine_id].empty()) {
    //             TaskId_t task_id = pending_tasks[machine_id].front();
    //             pending_tasks[machine_id].pop();
                
    //             if(!IsVMReady(machine.vm_id, machine.id)) {
    //                 pending_tasks[machine_id].push(task_id);
    //                 break;
    //             }
                
    //             unsigned req_mem = GetTaskMemory(task_id);
    //             MachineInfo_t machine_info = Machine_GetInfo(machine.id);
                
    //             if(machine_info.active_tasks < machine_info.num_cpus && req_mem <= (machine_info.memory_size - machine_info.memory_used)) {
    //                 VM_AddTask(machine.vm_id, task_id, MID_PRIORITY);
    //                 task_map[task_id] = machine.id;
    //                 machine.utilization = machine_info.active_tasks + 1;
    //                 machine.memory_used += req_mem;
    //             } else {
    //                 pending_tasks[machine_id].push(task_id);
    //                 break;
    //             }
    //         }
    //         break;
    //     }
    // }
}

