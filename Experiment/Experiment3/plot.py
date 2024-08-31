import matplotlib.pyplot as plt
import numpy as np
import re

def extract_reception_rate(line):
    match = re.search(r'reception_rate\s*=\s*([\d\.]+)', line)
    if match:
        return float(match.group(1))
    else:
        return None

# synthetics = ["tornado","neighbor","uniform_random","bit_complement"]
synthetics = ["bit_complement","shuffle","uniform_random"]
num_cpu = 64
# inj_rates = np.arange(0.05, 1.05, 0.05).tolist()
inj_rates = ["0.05","0.1","0.15","0.2","0.25",'0.3','0.35','0.4','0.45','0.5','0.55','0.6','0.65','0.7','0.75','0.8','0.85','0.9','0.95','1.0']
latencies = [1,2,4]

reception_rate = ({},{},{})

path = "Experiment/Experiment3/result/result2_largeth.txt"
with open(path, 'r') as file:
    lines = file.readlines()
    cnt = 0
    for synthetic in synthetics:
        for latency in latencies:
            for inj_rate in inj_rates:
                print(lines[10*cnt+2-1])
                print(f"{synthetic}_RandomDimension_inj{inj_rate}_latency{latency}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[10*cnt+2-1] == f"{synthetic}_RandomDimension_inj{inj_rate}_latency{latency}_numcpu{num_cpu}\n"
                reception_rate[0][(synthetic,latency,inj_rate)] = extract_reception_rate(lines[10*cnt+10-1])
                print(reception_rate[0][(synthetic,latency,inj_rate)])
                cnt+=1
    
    cnt = 0
    for synthetic in synthetics:
        for latency in latencies:
            for inj_rate in inj_rates:
                print(lines[10*cnt+2-1])
                print(f"{synthetic}_STAR_inj{inj_rate}_latency{latency}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[1802+9*cnt-1] == f"{synthetic}_STAR_inj{inj_rate}_latency{latency}_numcpu{num_cpu}\n"
                reception_rate[1][(synthetic,latency,inj_rate)] = extract_reception_rate(lines[1810+9*cnt-1])
                print(reception_rate[1][(synthetic,latency,inj_rate)])
                cnt+=1

fig, axs = plt.subplots(3, 3, figsize=(16, 16))
fig.subplots_adjust(hspace=0.5, wspace=0.4)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
for i, synthetic in enumerate(synthetics):
    for j, latency in enumerate(latencies):
        ax = axs[i, j]  # 选择正确的子图
        # for method in [range(2)]:
        for method in [1,0]: # 倒着颜色比较对
            reception_rate_ = [reception_rate[method].get((synthetic, latency, inj_rate), None) for inj_rate in inj_rates]
            line, = ax.plot(inj_rates, reception_rate_, marker='o', label=f'Method {method}')
            # 只在第一个子图时记录句柄和标签
            if i == 0 and j == 0:
                lines.append(line)
                if method == 0:
                    label = "Fully Adapt"
                elif method == 1:
                    label = "*-channel"
                labels.append(label)
        
        ax.set_title(f'{synthetic}, link/router latency = {latency}')
        ax.set_xlabel('Injection Rate')
        ax.set_ylabel('Reception Rate')
        ax.set_ylim(0, 1.05)
        # 设置横轴刻度，只显示第4,8,12,16,20个数据点
        selected_ticks = inj_rates[3:20:4]  # 选择第4,8,12,16,20个点
        ax.set_xticks(selected_ticks)

# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.05), ncol=3)

# 显示图像
plt.show()
plt.savefig("Experiment/Experiment3/figs/fig2.png")

