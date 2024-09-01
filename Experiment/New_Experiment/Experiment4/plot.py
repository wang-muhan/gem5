import matplotlib.pyplot as plt
import re

def extract_latency(line):
    match = re.search(r'average_packet_latency\s*=\s*([\d\.]+)', line)
    if match:
        return float(match.group(1))/2# 2是tick和cycle的比例
    else:
        return None

synthetics = ["shuffle","transpose","tornado","neighbor","uniform_random","bit_complement","bit_reverse","bit_rotation"]
num_cpu = 64
inj_rates = [0.2, 0.4, 0.6, 0.8, 1.0]
num_star_channels = [2,4,6,8,10,12,14,16]
latency = ({},{})

path = "Experiment/New_Experiment/Experiment4/result4.txt"
with open(path, 'r') as file:
    lines = file.readlines()
    cnt = 0
    current_line = 0
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_star_channel in num_star_channels:
                # print(lines[9*cnt+2-1])
                # print(f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n")
                # print(9*cnt+2-1)
                # # breakpoint()
                while lines[current_line] != f"{synthetic}_TORUSSTAR_inj{inj_rate}_numsc{num_star_channel}_numcpu64\n": #其实是Cube
                    # print(f"{synthetic}_TORUSSTAR_inj{inj_rate}_numsc{num_star_channel}_numcpu64\n")
                    # print(lines[current_line])
                    # breakpoint()
                    current_line += 1
                if lines[current_line+2][0]=='p':
                    cnt += 1
                    latency[0][(synthetic,num_star_channel,inj_rate)] = extract_latency(lines[current_line+6])
                    print(lines[current_line],lines[current_line+6],latency[0][(synthetic,num_star_channel,inj_rate)])
                # assert lines[9*cnt+2-1] == f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n"
    print(cnt)
    num_star_channels = [2,4,6,8]
    for synthetic in synthetics:
        for inj_rate in inj_rates:
            for num_star_channel in num_star_channels:
                # print(lines[9*cnt+2-1])
                # print(f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n")
                # print(9*cnt+2-1)
                # # breakpoint()
                while lines[current_line] != f"{synthetic}_TORUSSTAR_inj{inj_rate}_numsc{num_star_channel}_numcpu64\n": #其实是Cube
                    # print(f"{synthetic}_TORUSSTAR_inj{inj_rate}_numsc{num_star_channel}_numcpu64\n")
                    # print(lines[current_line])
                    # breakpoint()
                    current_line += 1
                if lines[current_line+2][0]=='p':
                    cnt += 1
                    latency[1][(synthetic,num_star_channel,inj_rate)] = extract_latency(lines[current_line+6])
                    print(lines[current_line],lines[current_line+6],latency[0][(synthetic,num_star_channel,inj_rate)])
                # assert lines[9*cnt+2-1] == f"{synthetic}_STAR_inj{inj_rate}_numcpu{num_cpu}\n"
    print(cnt)


fig, axs = plt.subplots(4, 4, figsize=(16, 12))
fig.subplots_adjust(hspace=0.5, wspace=0.5)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
num_star_channels = [2,4,6,8,10,12,14,16]
for i, synthetic in enumerate(synthetics):
    ax = axs[i//4,i%4]  # 选择正确的子图
    for inj_rate in inj_rates:
        latencies = [latency[0].get((synthetic, num_star_channel, inj_rate), None) for num_star_channel in num_star_channels]
        line, = ax.plot(num_star_channels, latencies, marker='o')
        # 只在第一个子图时记录句柄和标签
        if i == 0:
            lines.append(line)
            label = f"inj_rate = {inj_rate}"
            labels.append(label)
    ax.set_title(f'{synthetic}')
    ax.set_xlabel('num-star-channel')
    ax.set_ylabel('latency(cycle)')

# 遍历synthetics和num_cpus来填充每个小图
num_star_channels = [4,8,12,16]
for i, synthetic in enumerate(synthetics):
    ax = axs[2+i//4,i%4]  # 选择正确的子图
    for inj_rate in inj_rates:
        latencies = [latency[1].get((synthetic, num_star_channel/2, inj_rate), None) for num_star_channel in num_star_channels]
        line, = ax.plot(num_star_channels, latencies, marker='o')
        # 只在第一个子图时记录句柄和标签
        # if i == 0:
        #     lines.append(line)
        #     label = f"inj_rate = {inj_rate}"
        #     labels.append(label)
    # ax.set_title(f'{synthetic}')
    ax.set_title(f'{synthetic}')
    ax.set_xlabel('num-star-channel')
    ax.set_ylabel('latency(cycle)')
# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.05), ncol=5)
fig.text(0.06, 0.75, '6-Hypercube', va='center', rotation='vertical', fontsize=12, weight='bold')
fig.text(0.06, 0.25, '3-dim 4-ary torus', va='center', rotation='vertical', fontsize=12, weight='bold')

# 显示图像
plt.show()
plt.savefig("Experiment/New_Experiment/Experiment4/figs/ex4.png")
