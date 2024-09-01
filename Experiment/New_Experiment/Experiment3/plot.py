import matplotlib.pyplot as plt
import re

def extract_latency(line):
    match = re.search(r'average_packet_latency\s*=\s*([\d\.]+)', line)
    if match:
        return float(match.group(1))/2# 2是tick和cycle的比例
    else:
        return None

synthetics = ["shuffle","transpose","tornado","neighbor","uniform_random","bit_complement","bit_reverse","bit_rotation"]
num_cpus = [4, 16, 64]
inj_rates = [0.1, 0.2, 0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0]
latency = ({},{},{},{})

path = "Experiment/New_Experiment/Experiment1/result1.txt"
with open(path, 'r') as file:
    lines = file.readlines()
    cnt = 0
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_cpu in num_cpus:
                print(lines[9*cnt+2-1])
                print(f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n")
                print(9*cnt+2-1)
                # breakpoint()
                assert lines[9*cnt+2-1] == f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[0][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[9*cnt+8-1])
                print(latency[0][(synthetic,num_cpu,inj_rate)])
                cnt+=1

    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_cpu in num_cpus:
                print(lines[9*cnt+2-1])
                print(f"{synthetic}_routingalg1_inj{inj_rate}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[9*cnt+2-1] == f"{synthetic}_routingalg1_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[1][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[9*cnt+8-1])
                print(latency[1][(synthetic,num_cpu,inj_rate)])
                cnt+=1

    num_cpus = [16, 64]
    for num_cpu in num_cpus:
        for synthetic in synthetics:
            for inj_rate in inj_rates:
                print(lines[9*cnt+2-1])
                print(f"{synthetic}_TORUSSTAR_inj{inj_rate}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[9*cnt+2-1] == f"{synthetic}_TORUSSTAR_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[2][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[9*cnt+8-1])
                print(latency[2][(synthetic,num_cpu,inj_rate)])
                cnt+=1


path = "Experiment/New_Experiment/Experiment2/result2.txt" # 8^2
num_cpu=64
with open(path, 'r') as file:
    lines = file.readlines()
    cnt = 0
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            print(lines[9*cnt+2-1])
            print(f"{synthetic}_TORUSSTAR_inj{inj_rate}_numcpu{num_cpu}\n")
            print(9*cnt+2-1)
            # breakpoint()
            assert lines[9*cnt+2-1] == f"{synthetic}_TORUSSTAR_inj{inj_rate}_numcpu{num_cpu}\n"
            latency[3][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[9*cnt+8-1])
            print(latency[3][(synthetic,num_cpu,inj_rate)])
            cnt+=1

num_cpu=64
fig, axs = plt.subplots(2, 4, figsize=(16, 8))
fig.subplots_adjust(hspace=0.5, wspace=0.4)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
for i, synthetic in enumerate(synthetics):
    ax = axs[i//4,i%4]  # 选择正确的子图
    for method in [0,1,3]:
        latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
        line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
        # 只在第一个子图时记录句柄和标签
        if i == 0:
            lines.append(line)
            if method == 0:
                label = "Hypercube_*-channel (6-dim 2-ary) Torus"
            elif method == 1:
                label = "3-dim 4-ary Torus_*-channel"
            elif method == 3:
                label = "2-dim 8-ary Torus_*-channel"
            labels.append(label)
    
    ax.set_title(f'{synthetic}, CPU={num_cpu}')
    ax.set_xlabel('Injection Rate')
    ax.set_ylabel('Latency(cycle)')

# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.05), ncol=3)

# 显示图像
plt.show()
plt.savefig("Experiment/New_Experiment/Experiment2/figs/ex2.png")
