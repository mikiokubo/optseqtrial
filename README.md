# OptSeq Trial 
> Scheduling Optimization Solver Trial 


## How to Install to Jupyter Notebook (Labo) and/or Google Colaboratory

## Install

1. Clone from github:
> `!git clone https://github.com/mikiokubo/optseqtrial.git`
2. Move to optseq trial directry:
> `cd optseqtrial`

3. Change mode of execution file

 - for linux (Google Colab.)  
 > `!chmod +x optseq-linux`   

 - for Mac 
 > `!chmod +x optseq-mac`  

4. Import package and write a code:> `from optseqtrial.scop import *`
5. (Option) Install other packages if necessarily: 

> `!pip install plotly pandas numpy matplotlib`


## How to use

See https://mikiokubo.github.io/optseqtrial/  and  https://www.logopt.com/optseq/ 

Here is an example. 

```python
from optseqtrial.optseq import *
"""
Example 1
PERT
file name: Example1.py
Copyright Log Opt Co., Ltd.

Consider a 5-activity problem with precedence constraints between the activities.
Such a problem is called PERT (Program Evaluation and Review Technique).
The processing times (durations) of the activities are kept in the dictionary
 duration ={1:13, 2:25, 3:15, 4:27, 5:22 }.
Precedence constraints are given by:
 Activity 1 -> Activity 3; Activity 2 -> Activity 4;
 Activity 3 -> Activity 4; and Activity 3 -> Activity 5.
The objective is to find the maximum completion time (makespan) for all 5 activities.
"""

model = Model()
duration = {1: 13, 2: 25, 3: 15, 4: 27, 5: 22}
act = {}
mode = {}
for i in duration:
    act[i] = model.addActivity("Act[{0}]".format(i))
    mode[i] = Mode("Mode[{0}]".format(i), duration[i])
    act[i].addModes(mode[i])

# temporal (precedense) constraints
model.addTemporal(act[1], act[3])
model.addTemporal(act[2], act[4])
model.addTemporal(act[2], act[5])
model.addTemporal(act[3], act[4])

model.Params.TimeLimit = 1
model.Params.Makespan = True
model.Params.TimeLimit = 1
model.Params.Makespan = True
model.optimize()
```

    
     ================ Now solving the problem ================ 
    
    
    Solutions:
        source   ---     0     0
          sink   ---    55    55
        Act[1]   ---     0    13
        Act[2]   ---     0    25
        Act[3]   ---    13    28
        Act[4]   ---    28    55
        Act[5]   ---    25    47

