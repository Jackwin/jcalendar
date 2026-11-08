#ifndef JCALENDAR_PANEL_PAGE_H
#define JCALENDAR_PANEL_PAGE_H

#include <Arduino.h>

// Kept in flash; the configuration portal must work without Internet access.
static const char PANEL_PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light">
<title>J-Calendar · 面板管理</title>
<style>
:root{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#241e1b;background:#f5f1ea}
*{box-sizing:border-box}body{margin:0;padding:0 16px 36px}main{max-width:820px;margin:auto}
header{padding:28px 0 18px}h1{font-size:1.7rem;line-height:1.25;margin:7px 0}h2{font-size:1.15rem;margin:0 0 16px}
p{line-height:1.55;margin:7px 0;color:#675c56}.eyebrow{color:#9c372b;font-size:.76rem;font-weight:800;letter-spacing:.14em}
.card{background:#fff;border:1px solid #e8ded4;border-radius:18px;padding:20px;margin:0 0 14px;box-shadow:0 4px 18px #4934270a}
.hint{font-size:.86rem}.layout-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin-top:16px}
.layout{position:relative}.layout input{position:absolute;opacity:0}.layout span{display:flex;align-items:center;justify-content:center;text-align:center;min-height:64px;padding:9px;border:1px solid #ded1c7;border-radius:12px;background:#faf7f3;font-weight:650}
.layout input:checked+span{border:2px solid #ad392e;background:#fff0e9;color:#84271f}.layout input:focus-visible+span{outline:3px solid #e1a365;outline-offset:2px}
.row{display:flex;gap:8px;align-items:stretch}.row input{min-width:0;flex:1}input[type=text]{font:inherit;border:1px solid #d8c9bc;border-radius:10px;padding:12px;background:white}
button{font:inherit;cursor:pointer;border:1px solid #b54838;border-radius:10px;padding:10px 15px;min-height:44px;background:#fff;color:#8e3227;font-weight:650}
button.primary{background:#9d392e;color:white;border-color:#9d392e}button:disabled{opacity:.45;cursor:not-allowed}
.item-list{list-style:none;padding:0;margin:14px 0 0}.item-list li{display:flex;gap:9px;align-items:center;border-top:1px solid #f0e8e0;padding:10px 0;min-height:53px}
.item-list li span{flex:1;min-width:0;overflow-wrap:anywhere}.item-list li.done span{text-decoration:line-through;color:#877d76}
.item-list input[type=checkbox]{width:21px;height:21px;accent-color:#a33d31}.item-list button{border-color:#e0d4ca;color:#755d55;padding:8px 11px}
.days{display:grid;grid-template-columns:repeat(7,1fr);gap:5px;margin:14px 0}.days button{padding:7px 0;min-width:0;border-color:#e1d4c8}
.days button.active{background:#9d392e;color:white;border-color:#9d392e}.section-head{display:flex;align-items:baseline;justify-content:space-between;gap:8px}
.count{color:#806f65;font-size:.84rem}.footer{position:sticky;bottom:0;padding:12px 0 calc(12px + env(safe-area-inset-bottom));background:linear-gradient(#f5f1ea00,#f5f1ea 16px)}
.footer button{width:100%;min-height:52px}.message{min-height:24px;font-size:.9rem;color:#9d392e}.message.ok{color:#32714d}
a{color:#8e3227}small{color:#86786f}[hidden]{display:none!important}@media(max-width:480px){.card{padding:16px}.layout-grid{gap:6px}.layout span{font-size:.88rem}.days{gap:3px}.days button{font-size:.8rem}}
</style>
</head>
<body><main>
<header><div class="eyebrow">J-CALENDAR / LOCAL PANEL</div><h1>面板管理</h1><p>选择墨水屏模板，安排今天的课程和待办。内容保存在设备中，断网也能显示。</p></header>
<section class="card" aria-labelledby="layoutTitle"><h2 id="layoutTitle">显示模板</h2><p class="hint">点击模板即可切换下方内容；保存后设备刷新首页，单击设备按键仍可切换月历。</p>
<div class="layout-grid" id="layouts">
<label class="layout"><input type="radio" name="layout" value="dashboard"><span>日常看板</span></label>
<label class="layout"><input type="radio" name="layout" value="courses"><span>今日课程</span></label>
<label class="layout"><input type="radio" name="layout" value="tasks"><span>待办清单</span></label>
</div></section>
<section id="dashboardPanel" class="card" aria-labelledby="dashboardTitle"><h2 id="dashboardTitle">日常看板</h2><p>保留当前天气、额度和今日重点的显示方式。若要设置今日重点或网络服务，请返回设备设置。</p><p class="hint">课程和待办会继续保存在设备中，切换模板时不会丢失。</p></section>
<section id="taskPanel" class="card" aria-labelledby="taskTitle" hidden><div class="section-head"><h2 id="taskTitle">待办清单</h2><span class="count" id="taskCount"></span></div>
<form id="taskForm" class="row"><input id="taskText" type="text" maxlength="60" placeholder="例如：整理书包" aria-label="新待办" required><button type="submit">添加</button></form>
<ul id="taskList" class="item-list"></ul><p class="hint">最多 12 条；勾选完成后将不再显示在屏幕上。</p></section>
<section id="coursePanel" class="card" aria-labelledby="courseTitle" hidden><div class="section-head"><h2 id="courseTitle">今日课程</h2><span class="count" id="courseCount"></span></div>
<div id="days" class="days" role="group" aria-label="选择星期"></div>
<form id="courseForm" class="row"><input id="courseText" type="text" maxlength="60" placeholder="例如：08:00 数学 A101" aria-label="新课程" required><button type="submit">添加</button></form>
<ul id="courseList" class="item-list"></ul><p class="hint">最多 35 条；按录入顺序显示所选星期课程。</p></section>
<div class="footer"><div id="message" class="message" role="status" aria-live="polite"></div><button id="save" type="button" class="primary">保存并刷新墨水屏</button></div>
<p class="hint"><a href="/">返回设备设置</a> · 仅在配置窗口开放，保存后窗口会关闭。</p>
</main><script>
(() => {
  'use strict';
  const maxBytes = 60;
  const weekdays = ['周一','周二','周三','周四','周五','周六','周日'];
  const state = {version:1, layout:'dashboard', tasks:[], courses:[]};
  let selectedDay = 1;
  const byId = id => document.getElementById(id);
  const message = (text, ok = false) => {const el=byId('message');el.textContent=text;el.classList.toggle('ok',ok)};
  const bytes = value => new TextEncoder().encode(value).length;
  const validText = value => value.length > 0 && bytes(value) <= maxBytes;
  const action = (label, handler) => {const button=document.createElement('button');button.type='button';button.textContent=label;button.addEventListener('click',handler);return button};
  function renderLayout() {
    byId('dashboardPanel').hidden = state.layout !== 'dashboard';
    byId('taskPanel').hidden = state.layout !== 'tasks';
    byId('coursePanel').hidden = state.layout !== 'courses';
  }

  function renderTasks() {
    byId('taskCount').textContent = state.tasks.length + ' / 12';
    const list = byId('taskList');list.replaceChildren();
    state.tasks.forEach((task,index) => {
      const li=document.createElement('li');li.classList.toggle('done',task.done);
      const check=document.createElement('input');check.type='checkbox';check.checked=task.done;check.setAttribute('aria-label','完成 '+task.text);
      check.addEventListener('change',()=>{task.done=check.checked;renderTasks()});
      const label=document.createElement('span');label.textContent=task.text;
      li.append(check,label,action('删除',()=>{state.tasks.splice(index,1);renderTasks()}));list.append(li);
    });
    if (!state.tasks.length) {const li=document.createElement('li');li.textContent='还没有待办';list.append(li)}
  }
  function renderCourses() {
    byId('courseCount').textContent = state.courses.length + ' / 35';
    const days=byId('days');days.replaceChildren();
    weekdays.forEach((name,index)=>{const day=index+1;const button=action(name,()=>{selectedDay=day;renderCourses()});button.classList.toggle('active',day===selectedDay);button.setAttribute('aria-pressed',String(day===selectedDay));days.append(button)});
    const list=byId('courseList');list.replaceChildren();
    let shown=0;
    state.courses.forEach((course,index)=>{if(course.day!==selectedDay)return;shown++;
      const li=document.createElement('li');const label=document.createElement('span');label.textContent=course.text;
      li.append(label,action('删除',()=>{state.courses.splice(index,1);renderCourses()}));list.append(li);
    });
    if(!shown){const li=document.createElement('li');li.textContent='这一天还没有课程';list.append(li)}
  }
  document.querySelectorAll('input[name=layout]').forEach(input=>input.addEventListener('change',()=>{state.layout=input.value;renderLayout();message('模板已切换，记得保存')}));
  byId('taskForm').addEventListener('submit',event=>{event.preventDefault();const input=byId('taskText');const text=input.value.trim();
    if(!validText(text)){message('待办文字须为 1～60 UTF-8 字节');return}
    if(state.tasks.length>=12){message('待办最多 12 条');return}
    state.tasks.push({text,done:false});input.value='';renderTasks();message('待办已添加，记得保存');
  });
  byId('courseForm').addEventListener('submit',event=>{event.preventDefault();const input=byId('courseText');const text=input.value.trim();
    if(!validText(text)){message('课程文字须为 1～60 UTF-8 字节');return}
    if(state.courses.length>=35){message('课程最多 35 条');return}
    state.courses.push({day:selectedDay,text});input.value='';renderCourses();message('课程已添加，记得保存');
  });
  byId('save').addEventListener('click',async()=>{const button=byId('save');button.disabled=true;message('正在保存…');
    try{const response=await fetch('/api/panel',{method:'POST',headers:{'Content-Type':'application/json','X-Panel-Request':'1'},body:JSON.stringify(state)});
      if(!response.ok){const error=await response.json();throw new Error(error.error||'保存失败')}
      const saved=await response.json();
      if(saved.layout!==state.layout)throw new Error('设备回读的模板与所选模板不一致');
      message('已保存“'+({dashboard:'日常看板',courses:'今日课程',tasks:'待办清单'}[saved.layout])+'”，配置窗口正在关闭；刷屏约需 25～30 秒。',true);
    }catch(error){message(error.message||'保存失败，请重试');button.disabled=false}
  });
  async function load(){
    try{const response=await fetch('/api/panel',{cache:'no-store'});if(!response.ok)throw new Error('读取失败');const value=await response.json();
      if(value.version!==1||!Array.isArray(value.tasks)||!Array.isArray(value.courses))throw new Error('配置格式无效');
      state.layout=value.layout;state.tasks=value.tasks;state.courses=value.courses;
      const selected=document.querySelector('input[name=layout][value="'+state.layout+'"]');if(selected)selected.checked=true;
      renderLayout();
      renderTasks();renderCourses();message('已读取设备配置',true);
    }catch(error){message(error.message+'；请重新打开配置窗口');renderLayout();renderTasks();renderCourses()}
  }
  load();
})();
</script></body></html>)HTML";

#endif
