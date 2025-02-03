# MCCxMinecraftReborn plugin for Levilamina

![GitHub License](https://img.shields.io/github/license/Howie114514/MCCxMinecraftReborn-llplugin)
![GitHub Repo stars](https://img.shields.io/github/stars/Howie114514/MCCxMinecraftReborn-llplugin)

本插件需要配合[行为包](https://github.com/Howie114514/MCCxMinecraftReborn)使用

# 功能

### 实现通过指令发送 SetActorData 包修改玩家客户端实体的属性，使得每个玩家的客户端看到的东西能产生差异

###### （因为用途仅需修改布尔值，所以不编写修改其他类型值的相关程序）

`mccr syncprop <propname> <propvalue> <player>`

### 实现通过指令发送 SpawnParticleEffect 包向单个玩家发送粒子（类似于 java 版）

`mccr particle <particle> <pos:x y z> <player>`

实例：参考[network.ts](https://github.com/Howie114514/MCCxMinecraftReborn/blob/main/scripts/network.ts)
