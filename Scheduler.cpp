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

struct MachineLoad {
    MachineId_t id;
    unsigned utilization;
    unsigned memory_used;
    unsigned memory_size;
    bool active;
    VMId_t vm_id;
};

static vector<MachineLoad> cluster;
static unordered_map<VMId_t, bool> vm_ready;
static unordered_map<VMId_t, VMType_t> vm_types;  // Track VM types
static map<TaskId_t, MachineId_t> task_map;  // Track which machine each task is on
static unordered_map<MachineId_t, queue<TaskId_t>> pending_tasks;  // Tasks waiting for machine to power on

// Helper function to check if VM type is compatible with CPU type
static bool IsVMCompatibleWithCPU(VMType_t vm_type, CPUType_t cpu_type) {
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

// Helper function to verify VM is ready to receive tasks
static bool IsVMReady(VMId_t vm_id, MachineId_t machine_id) {
    if(vm_ready.find(vm_id) == vm_ready.end() || !vm_ready[vm_id]) {
        return false;
    }
    VMInfo_t vm_info = VM_GetInfo(vm_id);
    return (vm_info.machine_id == machine_id);
}

void Scheduler::Init() {
    unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total machines = " + to_string(total), 2);
    cluster.resize(total);

    for (unsigned i = 0; i < total; i++) {
        cluster[i].id = MachineId_t(i);
        MachineInfo_t info = Machine_GetInfo(cluster[i].id);
        
        cluster[i].vm_id = VM_Create(LINUX, info.cpu);
        vm_types[cluster[i].vm_id] = LINUX;
        cluster[i].utilization = info.active_tasks;
        cluster[i].active = true;
        cluster[i].memory_size = info.memory_size;
        cluster[i].memory_used = info.memory_used;

        // Machine_SetState(cluster[i].id, S5);
        // cluster[i].active = false;
        VM_Attach(cluster[i].vm_id, cluster[i].id);
        vm_ready[cluster[i].vm_id] = true;
    }

    SimOutput("Scheduler::Init(): Greedy scheduler initialized.", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    vm_ready[vm_id] = true;
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    VMType_t req_vm_type = RequiredVMType(task_id);
    CPUType_t req_cpu_type = RequiredCPUType(task_id);
    unsigned req_mem = GetTaskMemory(task_id);
    
    // Try to find a machine with matching CPU type and VM type
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        
        MachineInfo_t machine_info = Machine_GetInfo(machine.id);
        if(machine_info.cpu != req_cpu_type || machine_info.s_state != S0) continue;
        if(vm_ready.find(machine.vm_id) == vm_ready.end() || !vm_ready[machine.vm_id]) continue;
        if(vm_types.find(machine.vm_id) == vm_types.end() || vm_types[machine.vm_id] != req_vm_type) continue;
        
        unsigned active_tasks = machine_info.active_tasks;
        unsigned num_cpus = machine_info.num_cpus;
        unsigned avail_mem = machine_info.memory_size - machine_info.memory_used;
        
        if(active_tasks < num_cpus && req_mem <= avail_mem && IsVMReady(machine.vm_id, machine.id)) {
            VM_AddTask(machine.vm_id, task_id, MID_PRIORITY);
            machine.utilization = active_tasks + 1;
            machine.memory_used += req_mem;
            task_map[task_id] = machine.id;
            return;
        }
    }

    // Try to create a new VM on a matching active machine
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        
        MachineInfo_t machine_info = Machine_GetInfo(machine.id);
        if(machine_info.cpu != req_cpu_type || machine_info.s_state != S0) continue;
        if(!IsVMCompatibleWithCPU(req_vm_type, machine_info.cpu)) continue;
        
        unsigned active_tasks = machine_info.active_tasks;
        unsigned num_cpus = machine_info.num_cpus;
        unsigned avail_mem = machine_info.memory_size - machine_info.memory_used;
        
        if(active_tasks < num_cpus && req_mem <= avail_mem) {
            if(vm_ready[machine.vm_id]) {
                // TODO can't shutdown if VM has active tasks
                VM_Shutdown(machine.vm_id);
                vm_ready[machine.vm_id] = false;
            }
            
            VMId_t new_vm = VM_Create(req_vm_type, req_cpu_type);
            vm_types[new_vm] = req_vm_type;
            machine.vm_id = new_vm;
            VM_Attach(new_vm, machine.id);
            
            VMInfo_t vm_info = VM_GetInfo(new_vm);
            if(vm_info.machine_id == machine.id) {
                vm_ready[new_vm] = true;
                machine.utilization = active_tasks + 1;
                machine.memory_used += req_mem;
                VM_AddTask(new_vm, task_id, MID_PRIORITY);
                task_map[task_id] = machine.id;
                return;
            }
            vm_ready[new_vm] = false;
        }
    }
    
    // Try to power on an inactive machine with matching CPU type
    for(auto &machine: cluster) {
        if(machine.active) continue;
        
        MachineInfo_t info = Machine_GetInfo(machine.id);
        if(info.cpu != req_cpu_type || !IsVMCompatibleWithCPU(req_vm_type, info.cpu)) continue;
        
        Machine_SetState(machine.id, S0);
        VMId_t new_vm = VM_Create(req_vm_type, req_cpu_type);
        vm_types[new_vm] = req_vm_type;
        machine.vm_id = new_vm;
        machine.active = true;
        pending_tasks[machine.id].push(task_id);
        return;
    }
    
    SimOutput("SLA Violation: No available capacity for task " + to_string(task_id), 0);
}

void Scheduler::PeriodicCheck(Time_t now) {
    // Periodic optimization can be added here if needed
}

void Scheduler::Shutdown(Time_t time) {
    for(auto &machine: cluster) {
        if(machine.active && vm_ready[machine.vm_id]) {
            VM_Shutdown(machine.vm_id);
            Machine_SetState(machine.id, S5);
        }
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
    
    if(task_map.find(task_id) == task_map.end()) return;
    task_map.erase(task_id);
    
    // Update utilization and memory usage
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        MachineInfo_t info = Machine_GetInfo(machine.id);
        machine.utilization = info.active_tasks;
        machine.memory_used = info.memory_used;
    }
    
    // Consolidate workloads: migrate from lightly loaded to more utilized machines
    if(!migrating) {
        vector<pair<unsigned, MachineId_t>> machines_by_util;
        for(auto &m: cluster) {
            if(!m.active) continue;
            MachineInfo_t info = Machine_GetInfo(m.id);
            if(info.s_state == S0) {
                machines_by_util.push_back({m.utilization, m.id});
            }
        }
        sort(machines_by_util.begin(), machines_by_util.end());
        
        for(size_t i = 0; i < machines_by_util.size(); i++) {
            MachineId_t src_id = machines_by_util[i].second;
            unsigned u_src = machines_by_util[i].first;
            
            for(size_t j = i + 1; j < machines_by_util.size(); j++) {
                MachineId_t dst_id = machines_by_util[j].second;
                unsigned u_dst = machines_by_util[j].first;
                MachineInfo_t dst_info = Machine_GetInfo(dst_id);
                
                if(u_src + u_dst < dst_info.num_cpus && vm_ready[cluster[src_id].vm_id]) {
                    // VM_Migrate(cluster[src_id].vm_id, dst_id);
                    migrating = true;
                    vm_ready[cluster[src_id].vm_id] = false;
                    return;
                }
            }
        }
    }
    
    // Power down idle machines
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        MachineInfo_t info = Machine_GetInfo(machine.id);
        if(info.active_tasks == 0) {
            if(vm_ready[machine.vm_id]) {
                VM_Shutdown(machine.vm_id);
            }
            Machine_SetState(machine.id, S5);
            machine.active = false;
            vm_ready[machine.vm_id] = false;
        }
    }
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
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
    vm_ready[vm_id] = true;  // VM is ready after migration
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
    
    for(auto &machine: cluster) {
        if(machine.id == machine_id) {
            machine.active = true;
            
            VMInfo_t vm_info = VM_GetInfo(machine.vm_id);
            if(vm_info.machine_id != machine.id) {
                VM_Attach(machine.vm_id, machine.id);
                vm_info = VM_GetInfo(machine.vm_id);
            }
            vm_ready[machine.vm_id] = (vm_info.machine_id == machine.id);
            
            // Process pending tasks
            while(!pending_tasks[machine_id].empty()) {
                TaskId_t task_id = pending_tasks[machine_id].front();
                pending_tasks[machine_id].pop();
                
                if(!IsVMReady(machine.vm_id, machine.id)) {
                    pending_tasks[machine_id].push(task_id);
                    break;
                }
                
                unsigned req_mem = GetTaskMemory(task_id);
                MachineInfo_t machine_info = Machine_GetInfo(machine.id);
                
                if(machine_info.active_tasks < machine_info.num_cpus && req_mem <= (machine_info.memory_size - machine_info.memory_used)) {
                    VM_AddTask(machine.vm_id, task_id, MID_PRIORITY);
                    task_map[task_id] = machine.id;
                    machine.utilization = machine_info.active_tasks + 1;
                    machine.memory_used += req_mem;
                } else {
                    pending_tasks[machine_id].push(task_id);
                    break;
                }
            }
            break;
        }
    }
}

