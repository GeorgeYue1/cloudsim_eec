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

// struct MachineInformation {
//     MachineId_t machine_id;
//     vector<VMId_t> vms; 
// };

unordered_map<MachineId_t, vector<VMId_t>> running; 
unordered_map<MachineId_t, vector<VMId_t>> intermediate; 
unordered_map<MachineId_t, vector<VMId_t>> off; 
unordered_map<MachineId_t, Time_t> last_referenced;  


unordered_map<MachineId_t, vector<TaskId_t>> pending_tasks; 
unordered_map<MachineId_t, bool> ready; 
    
/**
 * Computes the pending execution time of a machine
 * based on sum of remaining instructions of all tasks divided by MIPS capacity.
 */
double pendingExecutionTime(MachineId_t machine_id) {
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    double total_instructions = 0.0;
    
    for(VMId_t vm_id : running[machine_id]) {
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        for(TaskId_t t_id : vm_info.active_tasks) {
            TaskInfo_t t_info = GetTaskInfo(t_id);
            total_instructions += (double)t_info.remaining_instructions;
        }
    }

    double total_mips = (double)machine_info.performance[machine_info.p_state] * machine_info.num_cpus;
    if (total_mips == 0) return DBL_MAX;  // avoid division by zero
    return total_instructions / (total_mips * 1e6); // in seconds
}


/**
 * Checks if the machine can service the given task
 */
bool canScheduleTask(TaskId_t task_id, MachineId_t machine_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);

    if((machine_info.memory_used + task_info.required_memory) > machine_info.memory_size) {
        return false; 
    }

    double PET = pendingExecutionTime(machine_id); 
    // cout << "PET: " << PET << endl;
    // cout << "task_info.target_completion: " << task_info.target_completion << endl;
    // cout << "task_info.arrival: " << task_info.arrival << endl;

    double target_time_sec = max(1e-6, (double)(task_info.target_completion - task_info.arrival) / 1e6);

    double total_mips = (double)machine_info.performance[machine_info.p_state] * machine_info.num_cpus;
    double task_execution_time = (double)task_info.total_instructions / (total_mips *1e6);

    switch(task_info.required_sla) {
        case SLA0:
            // cout << task_execution_time + PET + 1 << " " << target_time_sec << endl;
            return task_execution_time + PET + 5 <= target_time_sec;
        case SLA1:
            return task_execution_time + PET + 1 <= target_time_sec;
        case SLA2:
            return task_execution_time + PET + 2.5 <= target_time_sec;
        case SLA3:
            return task_execution_time + PET <= target_time_sec;
    }

}

/**
 * Prints all information for a specific machine
 * Cred: ChatGPT :) 
 */
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
 * Otherwise, returns UINT_MAX; 
 */
VMId_t isTaskCompatible(TaskId_t task_id, MachineInfo_t machine_info) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 
    if(task_info.required_cpu != machine_info.cpu) {
        return UINT_MAX; 
    }

    for(VMId_t vm_id : running[machine_info.machine_id]) {
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

    for(unsigned i = 0; i < total; i++) {
        MachineId_t machine_id = MachineId_t(i);
        if(i < total / 3) {
            running[machine_id];
            ready[machine_id] = true; 
        } else if(i < 2*total/3){
            ready[machine_id] = true; 
            intermediate[machine_id];
        } else {
            ready[machine_id] = false; 
            off[machine_id];
            Machine_SetState(machine_id, S5); 
        }
        last_referenced[machine_id] = 0; 
    }

    SimOutput("Scheduler::Init(): Greedy scheduler initialized.", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
}


/**
 * Turns off all idle machines
 */
void turnOffIdleMachines(Time_t now) {
    for(auto &[machine_id, vms] : intermediate) {
        MachineInfo_t machine_info = Machine_GetInfo(machine_id); 
        bool shutDownMachine = true; 
        for(VMId_t vm_id : vms) {
            VMInfo_t vm_info = VM_GetInfo(vm_id); 
            if(!vm_info.active_tasks.empty()) {
                shutDownMachine = false; 
                break; 
            }
        }
        
        // Machine hasn't been used in the last 10 minutes
        if(shutDownMachine && (now - last_referenced[machine_id] >= (uint64_t)600 * 1e6) ) {
            // for(VMId_t vm_id : machine.vms) {
            //     VM_Shutdown(vm_id); 
            // }
            if(machine_info.s_state != S5 && ready[machine_id])  {
                // cout << "Machine " << machine.machine_id << " turned off" << endl; 
                SimOutput("Machine " + to_string(machine_info.machine_id) + " turned off", 1); 
                ready[machine_id] = false; 
                Machine_SetState(machine_id, S5); 
            }
        }
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id); 

    // if((task_id % 3000) == 0 && task_id != 0) {
    //     SimOutput("Turning off idle machines", 1); 
    //     turnOffIdleMachines(now); 
    // }

    for(auto &[machine_id, vms] : running) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        if(!ready[machine_id] || m_info.cpu != task_info.required_cpu) {
            continue; 
        } 

        if(canScheduleTask(task_id, machine_id)) {
            VMId_t vm_id = isTaskCompatible(task_id, m_info);
            if(vm_id == UINT_MAX) {
                vm_id = VM_Create(task_info.required_vm, task_info.required_cpu);
                VM_Attach(vm_id, machine_id);
                running[machine_id].push_back(vm_id);
            }
            last_referenced[machine_id] = now; 
            VM_AddTask(vm_id, task_id, MID_PRIORITY);
            SimOutput("Task assigned to machine  " + to_string(machine_id), 1);
            return;
        }
    }

    MachineId_t erase_id = UINT_MAX; 
    for(auto &[machine_id, vms] : intermediate) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        if(!ready[machine_id] || m_info.cpu != task_info.required_cpu) {
            continue;
        }
        VMId_t vm_id = VM_Create(task_info.required_vm, task_info.required_cpu);
        VM_Attach(vm_id, machine_id);
        running[machine_id].push_back(vm_id);
        VM_AddTask(vm_id, task_id, MID_PRIORITY);
        last_referenced[machine_id] = now; 
        erase_id = machine_id; 
        break; 
    }

    if(erase_id != UINT_MAX) {
        intermediate.erase(erase_id); 
        return; 
    }      


    for(auto &[machine_id, vms] : off) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        if(m_info.s_state != S0 || m_info.cpu != task_info.required_cpu) {
            continue; 
        }
        if(ready[machine_id]) {
            ready[machine_id] = false;
            Machine_SetState(machine_id, S0);
        }
        pending_tasks[machine_id].push_back(task_id);
        return;
    }


    // No machine can schedule ask, allocate it to a random active machine
    for(auto &[machine_id, vms] : running) {
        MachineInfo_t m_info = Machine_GetInfo(machine_id);
        if(m_info.cpu != task_info.required_cpu) {
            continue; 
        } 

        VMId_t vm_id = isTaskCompatible(task_id, m_info);
        if(vm_id == UINT_MAX) {
            vm_id = VM_Create(task_info.required_vm, task_info.required_cpu);
            VM_Attach(vm_id, machine_id);
        }
        VM_AddTask(vm_id, task_id, MID_PRIORITY);
        running[machine_id].push_back(vm_id);
        return; 
    }
    SimOutput("NewTask(): Could not schedule task " + to_string(task_id), 1);
}



void Scheduler::PeriodicCheck(Time_t now) {
    // Periodic optimization can be added here if needed
}

void Scheduler::Shutdown(Time_t time) {
    for(auto &[machine_id, vms] : running) {
        for(VMId_t vm_id: vms) {
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
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 4);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    migrating = false; 
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
    
    // for(auto &machine : cluster) {
    //     // printMachineInfo(machine.machine_id);
    // }

    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {

}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " state change complete at " + to_string(time), 3);
    if(pending_tasks.find(machine_id) == pending_tasks.end()) {
        return; 
    }

    ready[machine_id] = true; 
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if(machine_info.s_state == S0) {
        for(TaskId_t task_id: pending_tasks[machine_id]) {
            TaskInfo_t task_info = GetTaskInfo(task_id); 
            VMId_t vm_id = VM_Create(task_info.required_vm, task_info.required_cpu); 
            VM_Attach(vm_id, machine_id); 
            running[machine_id].push_back(vm_id); 
            off.erase(machine_id); 
            VM_AddTask(vm_id, task_id, MID_PRIORITY); 
        }
        pending_tasks.erase(machine_id); 
    }

}


