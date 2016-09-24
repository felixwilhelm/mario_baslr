# mario_baslr

This repository contains a small Proof-of-Concept tool for leaking the base address of the KVM hypervisor kernel module (kvm.ko) from a guest VM. It does this by using a timing side-channel created by collisions in the branch target buffer (BTB) of modern Intel CPUs.
This approach is based on the great research paper ["Jump Over ASLR: Attacking Branch Predictors to Bypass ASLR"] (http://www.cs.binghamton.edu/~dima/micro16.pdf) by Dmitry Evtyushkin, Dmitry Ponomarev and Nael Abu-Ghazaleh. 

Interestingly, the authors of the original paper don't seem to have realised that their technique is not only usable for attacks against KASLR or other user-space tools but also works regardless of virtualization boundaries. This is an important difference to other hardware based timing
attacks such as `prefetch`, which can only be used for addresses that are mapped in the execution context of the attacker. 

In theory the BTB side-channel offers a generic way to bypass hypervisor/host ASLR in virtualized environments. However, there are a number of important restrictions:
* As discussed in the linked paper, the BTB only uses bits 0-30 as hash input. This means ASLR implementations that also randomize the most significant bits of virtual addresses can only be weakened.
* The BTB hashing mechanism does not seem to be very collision safe. This means the PoC tool might not always find a unique  base addresses and return multiple guesses.
* The attacker needs a way to trigger execution of control-flow instructions in the target using its own CPU core. This is relatively easy for hypervisor code (by triggering a VM exit) but might be more difficult when targeting worker processes or device backends.

Only the second issue has an impact when targeting the KVM kernel module, making KVM the easiest target for this attack. 

The offsets used in the PoC are targeting kvm.ko compiled for Ubuntu 16.04 with a 4.4.0-38-generic kernel. Future versions might 
include a fingerprinting mechanism to make this usable in the real world.
