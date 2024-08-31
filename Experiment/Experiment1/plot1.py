import matplotlib.pyplot as plt
import re

def extract_latency(line):
    match = re.search(r'average_packet_latency\s*=\s*([\d\.]+)', line)
    if match:
        return float(match.group(1))
    else:
        return None

synthetics = ["tornado","neighbor","uniform_random","bit_complement"]
num_cpus = [4, 16, 64, 256]
inj_rates = [0.1, 0.2, 0.5, 1.0]
latency = ({},{},{})

path = "Experiment/Experiment1/result.txt"
with open(path, 'r') as file:
    lines = file.readlines()
    cnt = 0
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_cpu in num_cpus:
                print(lines[10*cnt+2-1])
                print(f"{synthetic}_DOR_inj{inj_rate}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[10*cnt+2-1] == f"{synthetic}_DOR_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[0][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[10*cnt+8-1])
                print(latency[0][(synthetic,num_cpu,inj_rate)])
                cnt+=1
    
    cnt = 0
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_cpu in num_cpus:
                print(lines[642+9*cnt-1])
                print(f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[642+9*cnt-1] == f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[1][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[648+9*cnt-1])
                print(latency[1][(synthetic,num_cpu,inj_rate)])
                cnt+=1

    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_cpu in num_cpus:
                print(lines[642+9*cnt-1])
                print(f"{synthetic}_routingalg1_inj{inj_rate}_numcpu{num_cpu}\n")
                # breakpoint()
                assert lines[642+9*cnt-1] == f"{synthetic}_routingalg1_inj{inj_rate}_numcpu{num_cpu}\n"
                latency[2][(synthetic,num_cpu,inj_rate)] = extract_latency(lines[648+9*cnt-1])
                print(latency[2][(synthetic,num_cpu,inj_rate)])
                cnt+=1


fig, axs = plt.subplots(4, 4, figsize=(16, 16))
fig.subplots_adjust(hspace=0.5, wspace=0.4)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
for i, synthetic in enumerate(synthetics):
    for j, num_cpu in enumerate(num_cpus):
        ax = axs[i, j]  # 选择正确的子图
        for method in range(3):
            latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
            line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
            # 只在第一个子图时记录句柄和标签
            if i == 0 and j == 0:
                lines.append(line)
                if method == 0:
                    label = "Hypercube_DOR"
                elif method == 1:
                    label = "Hypercube_*-channel"
                elif method == 2:
                    label = "Mesh_XY"
                labels.append(label)
        
        ax.set_title(f'{synthetic}, CPU={num_cpu}')
        ax.set_xlabel('Injection Rate')
        ax.set_ylabel('Latency')

# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 1.05), ncol=3)

# # 遍历synthetics和num_cpus来填充每个小图
# for i, synthetic in enumerate(synthetics):
#     for j, num_cpu in enumerate(num_cpus):
#         ax = axs[i, j]  # 选择正确的子图
#         for method in range(3):
#             latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
#             if method == 0:
#                 label = "Hypercube_DOR"
#             elif method == 1:
#                 label = "Hypercube_*-channel"
#             elif method == 2:
#                 label = "Mesh_XY"
#             ax.plot(inj_rates, latencies, marker='o', label=label)
        
#         ax.set_title(f'{synthetic}, CPU={num_cpu}')
#         ax.set_xlabel('Injection Rate')
#         ax.set_ylabel('Latency')
#         ax.legend()

# 显示图像
plt.show()
plt.savefig("Experiment/Experiment1/ex1_fig1.png")

