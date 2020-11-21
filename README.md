# OptSeq Trial 
> Scheduling Optimization Solver Trial 


## How to Install to Jupyter Notebook (Labo) and/or Google Colaboratory

## Install

1. Clone from github:
> `!git clone https://github.com/mikiokubo/optseqtrial.git`
2. Move to scoptrial directry:
> `cd scoptrial`

3. Change mode of execution file

 - for linux (Google Colab.)  
 > `!chmod +x optseq-linux`   

 - for Mac 
 > `!chmod +x optseq-mac`  

4. Import package and write a code:> `from optseqtrial.scop import *`
5. (Option) Install other packages if necessarily: 

> `!pip install plotly`


## How to use

See https://mikiokubo.github.io/optseqtrial/  and  https://www.logopt.com/optseq/ 

Here is an example. 

```python
from optseqtrial.optseq import *
model = Model()
duration = {1: 13, 2: 25, 3: 15, 4: 27, 5: 22}
act = {}
mode = {}
res=model.addResource("worker",capacity=1)
for i in duration:
    act[i] = model.addActivity(f"Act[{i}]")
    mode[i] = Mode(f"Mode[{i}]", duration[i])
    mode[i].addResource(res,requirement=1)
    act[i].addModes(mode[i])
        
# temporal (precedense) constraints
model.addTemporal(act[1], act[2], delay = 20)
model.addTemporal(act[1], act[3], delay = 20)
model.addTemporal(act[2], act[4], delay=10)
model.addTemporal(act[2], act[5], delay = 8)
model.addTemporal(act[3], act[4], delay =10)

model.Params.TimeLimit = 1
model.Params.Makespan = True
model.optimize()
```

    
     ================ Now solving the problem ================ 
    
    
    Solutions:
        source   ---     0     0
          sink   ---   122   122
        Act[1]   ---     0    13
        Act[2]   ---    33    58
        Act[3]   ---    58    73
        Act[4]   ---    95   122
        Act[5]   ---    73    95

