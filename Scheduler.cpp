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
static unsigned active_machines = 16;

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
static map<TaskId_t, MachineId_t> task_map;  // Track which machine each task is on

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    // 

    // SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    // SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    // for(unsigned i = 0; i < active_machines; i++)
    //     vms.push_back(VM_Create(LINUX, X86));
    // for(unsigned i = 0; i < active_machines; i++) {
    //     machines.push_back(MachineId_t(i));
    // }    
    // for(unsigned i = 0; i < active_machines; i++) {
    //     VM_Attach(vms[i], machines[i]);
    // }

    // bool dynamic = false;
    // if(dynamic)
    //     for(unsigned i = 0; i<4 ; i++)
    //         for(unsigned j = 0; j < 8; j++)
    //             Machine_SetCorePerformance(MachineId_t(0), j, P3);
    // // Turn off the ARM machines
    // for(unsigned i = 24; i < Machine_GetTotal(); i++)
    //     Machine_SetState(MachineId_t(i), S5);

    // SimOutput("Scheduler::Init(): VM ids are " + to_string(vms[0]) + " ahd " + to_string(vms[1]), 3);

    unsigned total = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total machines = " + to_string(total), 2);
    cluster.resize(total);

    for (unsigned i = 0; i < total; i++) {
        cluster[i].id = MachineId_t(i);
        
        // Get machine info to determine CPU type
        MachineInfo_t info = Machine_GetInfo(cluster[i].id);
        CPUType_t machine_cpu = info.cpu;  // Get the machine's CPU type
        
        // Create VM with LINUX (works with all CPU types) and matching CPU type
        cluster[i].vm_id = VM_Create(LINUX, machine_cpu);
        cluster[i].utilization = info.active_tasks;  // Initialize with current active tasks
        // cluster[i].active = (i < active_machines);
        cluster[i].active = true;

        cluster[i].memory_size = info.memory_size;
        cluster[i].memory_used = info.memory_used;

        if (cluster[i].active) {
            VM_Attach(cluster[i].vm_id, cluster[i].id);
            vm_ready[cluster[i].vm_id] = true;  // VM is ready after attachment
        } else {
            Machine_SetState(cluster[i].id, S5);
            vm_ready[cluster[i].vm_id] = false;  // VM not ready if machine is off
        }
    }

    SimOutput("Scheduler::Init(): Greedy scheduler initialized.", 2);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
    vm_ready[vm_id] = true;  // VM is ready after migration completes
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM, 
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired

    // Priority_t priority = (task_id == 0 || task_id == 64)? HIGH_PRIORITY : MID_PRIORITY;
    // if(migrating) {
    //     VM_AddTask(vms[0], task_id, priority);
    // }
    // else {
    //     VM_AddTask(vms[task_id % active_machines], task_id, priority);
    // }// Skeleton code, you need to change it according to your algorithm

    for(auto &machine: cluster) {
        if(!machine.active) continue;
        
        // Check if VM is ready (attached and not migrating)
        if(!vm_ready[machine.vm_id]) continue;
        
        // Get current machine state
        MachineInfo_t info = Machine_GetInfo(machine.id);
        unsigned active_tasks = info.active_tasks;
        unsigned num_cpus = info.num_cpus;
        unsigned avail_mem = info.memory_size - info.memory_used;
        unsigned req_mem = GetTaskMemory(task_id);
        
        // Check if machine has available CPUs and memory
        if(active_tasks < num_cpus && req_mem <= avail_mem) {
            // Update utilization based on actual CPU usage
            machine.utilization = active_tasks + 1;  // Will be +1 after task is added
            machine.memory_used += req_mem;
            VM_AddTask(machine.vm_id, task_id, MID_PRIORITY);
            task_map[task_id] = machine.id;  // Track which machine this task is on
            return;
        }
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto &machine: cluster) {
        if(machine.active) {
            // Only shut down VM if it's actually attached/ready
            if(vm_ready[machine.vm_id]) {
                VM_Shutdown(machine.vm_id);
            }
            Machine_SetState(machine.id, S5);
        }
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
    
    // Find which machine this task was on
    if(task_map.find(task_id) == task_map.end()) return;
    MachineId_t host_machine = task_map[task_id];
    task_map.erase(task_id);
    
    // Update utilization and memory usage of all machines
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        MachineInfo_t info = Machine_GetInfo(machine.id);
        machine.utilization = info.active_tasks;  // Update based on actual active tasks
        machine.memory_used = info.memory_used;
    }
    
    // Try to consolidate workloads (skip if migration ongoing)
    if(!migrating) {
        // Sort machines by ascending utilization (only active machines)
        vector<pair<unsigned, MachineId_t>> machines_by_util;
        for(auto &m: cluster) {
            if(!m.active) continue;
            MachineInfo_t info = Machine_GetInfo(m.id);
            // Only consider machines that are powered on (S0)
            if(info.s_state == S0) {
                machines_by_util.push_back({m.utilization, m.id});
            }
        }
        sort(machines_by_util.begin(), machines_by_util.end());
        
        // Migrate VMs from lightly loaded machines to more utilized ones
        // Check if u_src + u_dst < num_cores of destination
        bool migration_found = false;
        for(size_t i = 0; i < machines_by_util.size() && !migration_found; i++) {
            MachineId_t src_id = machines_by_util[i].second;
            unsigned u_src = machines_by_util[i].first;
            
            // Try destinations with higher utilization
            for(size_t j = i + 1; j < machines_by_util.size() && !migration_found; j++) {
                MachineId_t dst_id = machines_by_util[j].second;
                unsigned u_dst = machines_by_util[j].first;
                
                // Get destination machine info to check num_cores
                MachineInfo_t dst_info = Machine_GetInfo(dst_id);
                unsigned dst_num_cores = dst_info.num_cpus;
                
                // Check if migration is feasible: u_src + u_dst < num_cores of destination
                // Destination must be active and in S0 state (already filtered above)
                if(u_src + u_dst < dst_num_cores && vm_ready[cluster[src_id].vm_id]) {
                    VM_Migrate(cluster[src_id].vm_id, dst_id);
                    migrating = true;
                    vm_ready[cluster[src_id].vm_id] = false;  // VM not ready during migration
                    SimOutput("TaskComplete(): Migrating VM " + to_string(cluster[src_id].vm_id) +
                              " from machine " + to_string(src_id) + " (u=" + to_string(u_src) + 
                              ") -> machine " + to_string(dst_id) + " (u=" + to_string(u_dst) + 
                              "), total=" + to_string(u_src + u_dst) + " < " + to_string(dst_num_cores) + " cores", 2);
                    migration_found = true;
                }
            }
        }
    }
    
    // Power down idle machines: if no running tasks -> shut down VM + set state to S5
    for(auto &machine: cluster) {
        if(!machine.active) continue;
        MachineInfo_t info = Machine_GetInfo(machine.id);
        if(info.active_tasks == 0) {
            // Only shut down VM if it's actually attached/ready
            if(vm_ready[machine.vm_id]) {
                VM_Shutdown(machine.vm_id);
            }
            Machine_SetState(machine.id, S5);
            machine.active = false;
            vm_ready[machine.vm_id] = false;
            SimOutput("TaskComplete(): Machine " + to_string(machine.id) + " powered off (idle)", 2);
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
    // Called in response to an earlier request to change the state of a machine
    SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " state change complete at " + to_string(time), 3);
    
    // Find the machine and mark its VM as ready
    for(auto &machine: cluster) {
        if(machine.id == machine_id) {
            machine.active = true;
            
            // Check if VM is already attached to this machine
            VMInfo_t vm_info = VM_GetInfo(machine.vm_id);
            if(vm_info.machine_id != machine.id) {
                // VM is not attached to this machine, so attach it
                VM_Attach(machine.vm_id, machine.id);
            }
            vm_ready[machine.vm_id] = true;  // VM is ready after attachment
            break;
        }
    }
}

