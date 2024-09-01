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
latency = ({},{},{})

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


# fig, axs = plt.subplots(8, 3, figsize=(18, 18))
# fig.subplots_adjust(hspace=0.8, wspace=0.4)

# lines = []
# labels = []
# num_cpus = [16,64,256] # 4不要了

# # 遍历synthetics和num_cpus来填充每个小图
# for i, synthetic in enumerate(synthetics):
#     for j, num_cpu in enumerate(num_cpus):
#         ax = axs[i, j]  # 选择正确的子图
#         for method in range(2):
#             latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
#             line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
#             # 只在第一个子图时记录句柄和标签
#             if i == 0 and j == 1:
#                 lines.append(line)
#                 if method == 0:
#                     label = "Hypercube_*-channel"
#                 elif method == 1:
#                     label = "Mesh_XY"
#                 labels.append(label)
        
#         ax.set_title(f'{synthetic}, CPU={num_cpu}')
#         ax.set_xlabel('Injection Rate')
#         ax.set_ylabel('Latency')

# # 在整个图中添加一个全局图例，并将图例放在图像之外
# fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.07), ncol=2)

# # 显示图像
# plt.show()
# plt.savefig("Experiment/Experiment1/figs/new.png", bbox_inches='tight')

# import matplotlib.pyplot as plt
# import matplotlib.gridspec as gridspec
# import matplotlib.lines as mlines

# fig = plt.figure(figsize=(18, 18))
# # 定义gridspec布局
# outer = gridspec.GridSpec(1, 2, width_ratios=[1, 1], wspace=0.2)

# # 创建左右两部分
# left_grid = gridspec.GridSpecFromSubplotSpec(4, 2, subplot_spec=outer[0], hspace=0.8, wspace=0.6)
# right_grid = gridspec.GridSpecFromSubplotSpec(4, 2, subplot_spec=outer[1], hspace=0.8, wspace=0.6)

# lines = []
# labels = []
# num_cpus = [16, 64]  # 4不要了

# # 遍历synthetics和num_cpus来填充每个小图
# for i, synthetic in enumerate(synthetics[:4]):
#     for j, num_cpu in enumerate(num_cpus):
#         ax = fig.add_subplot(left_grid[i, j])  # 左侧部分
#         for method in range(2):
#             latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
#             line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
#             # 只在第一个子图时记录句柄和标签
#             if i == 0 and j == 1:
#                 lines.append(line)
#                 if method == 0:
#                     label = "Hypercube_*-channel"
#                 elif method == 1:
#                     label = "Mesh_XY"
#                 labels.append(label)
        
#         ax.set_title(f'{synthetic}, CPU={num_cpu}')
#         ax.set_xlabel('Injection Rate')
#         ax.set_ylabel('Latency')

# # 右侧部分
# for i, synthetic in enumerate(synthetics[4:]):
#     for j, num_cpu in enumerate(num_cpus):
#         ax = fig.add_subplot(right_grid[i, j])
#         for method in range(2):
#             latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
#             ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
        
#         ax.set_title(f'{synthetic}, CPU={num_cpu}')
#         ax.set_xlabel('Injection Rate')
#         ax.set_ylabel('Latency')

# # 在整个图中添加一个全局图例，并将图例放在图像下方
# fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.07), ncol=2)

# # # 在中间绘制虚线分割
# # fig.text(0.5, 0.5, '', ha='center', va='center')
# # plt.axvline(x=0.495, color='black', linestyle='--', linewidth=2, transform=fig.transFigure, clip_on=False)

# # 手动绘制虚线分割线
# line = mlines.Line2D([0.5, 0.5], [0.1, 0.9], color='black', linestyle='--', linewidth=2, transform=fig.transFigure, clip_on=False)
# fig.add_artist(line)

# # 显示图像
# plt.show()
# plt.savefig("Experiment/New_Experiment/Experiment1/figs/new.png", bbox_inches='tight')

num_cpu=16
fig, axs = plt.subplots(2, 4, figsize=(16, 8))
fig.subplots_adjust(hspace=0.5, wspace=0.4)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
for i, synthetic in enumerate(synthetics):
    ax = axs[i//4,i%4]  # 选择正确的子图
    for method in range(2):
        latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
        line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
        # 只在第一个子图时记录句柄和标签
        if i == 0:
            lines.append(line)
            if method == 0:
                label = "Hypercube_*-channel"
            elif method == 1:
                label = "Mesh_XY"
            # elif method == 2:
            #     label = "2-dim 4-ary Torus_*-channel"
            labels.append(label)
    
    ax.set_title(f'{synthetic}, CPU={num_cpu}')
    ax.set_xlabel('Injection Rate')
    ax.set_ylabel('Latency(cycle)')

# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.05), ncol=3)

# 显示图像
plt.show()
plt.savefig("Experiment/New_Experiment/Experiment1/figs/16.png")

num_cpu=64
fig, axs = plt.subplots(2, 4, figsize=(16, 8))
fig.subplots_adjust(hspace=0.5, wspace=0.4)

lines = []
labels = []

# 遍历synthetics和num_cpus来填充每个小图
for i, synthetic in enumerate(synthetics):
    ax = axs[i//4,i%4]  # 选择正确的子图
    for method in range(2):
        latencies = [latency[method].get((synthetic, num_cpu, inj_rate), None) for inj_rate in inj_rates]
        line, = ax.plot(inj_rates, latencies, marker='o', label=f'Method {method}')
        # 只在第一个子图时记录句柄和标签
        if i == 0:
            lines.append(line)
            if method == 0:
                label = "Hypercube_*-channel"
            elif method == 1:
                label = "Mesh_XY"
            # elif method == 2:
            #     label = "3-dim 4-ary Torus_*-channel"
            labels.append(label)
    
    ax.set_title(f'{synthetic}, CPU={num_cpu}')
    ax.set_xlabel('Injection Rate')
    ax.set_ylabel('Latency(cycle)')

# 在整个图中添加一个全局图例
fig.legend(lines, labels, loc='upper center', bbox_to_anchor=(0.5, 0.05), ncol=3)

# 显示图像
plt.show()
plt.savefig("Experiment/New_Experiment/Experiment1/figs/64.png")
