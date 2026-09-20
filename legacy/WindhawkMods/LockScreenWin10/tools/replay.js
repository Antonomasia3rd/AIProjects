// Pure replay logic shared by the offline browser report and its Node tests.
const liveEventNames = new Set(['liveElement','propertyChanged','geometryChanged','stateChanging',
  'stateChanged','liveStart','liveReady','liveStop','add','remove','outputGap','monitorError','captureStopped','viewChanged']);
function replayNodes(baseline, events, atMs) {
  const nodes = new Map(JSON.parse(JSON.stringify(baseline)).map(n=>[n.handle,n]));
  for (const event of events) {
    if ((event.elapsedMs||0)>atMs) continue;
    const kind=event.event, handle=event.handle;
    if (kind==='add' && !nodes.has(handle)) {
      for (const sibling of nodes.values()) if(sibling.parent===event.parent && sibling.childIndex>=event.childIndex) sibling.childIndex++;
      nodes.set(handle,{...event,properties:[],effective:{}});
    } else if (kind==='liveElement') {
      const node=nodes.get(handle)||{handle,properties:[]};
      for (const [key,value] of Object.entries(event)) if(!['event','elapsedMs','properties','effective'].includes(key)) node[key]=value;
      node.effective ||= {};
      for (const property of event.properties||[]) node.effective[property.name]=property.value;
      Object.assign(node.effective,event.effective||{}); nodes.set(handle,node);
    } else if (kind==='remove' && nodes.has(handle)) {
      const removed=nodes.get(handle), pending=[handle];
      while(pending.length) {
        const current=pending.pop();
        for(const [key,node] of nodes) if(node.parent===current && key!==current) pending.push(key);
        nodes.delete(current);
      }
      for(const sibling of nodes.values()) if(sibling.parent===removed.parent && sibling.childIndex>removed.childIndex) sibling.childIndex--;
    } else if (nodes.has(handle)) {
      const node=nodes.get(handle);
      if(kind==='propertyChanged') { node.effective ||= {}; node.effective[event.property]=event.value; }
      if(kind==='geometryChanged') node.rectangle=event.rectangle;
      if(kind==='viewChanged') {
        node.effective ||= {};
        Object.assign(node.effective,{HorizontalOffset:event.horizontalOffset,VerticalOffset:event.verticalOffset,ZoomFactor:event.zoomFactor});
      }
      if(kind==='stateChanged') {
        node.visualStateGroups ||= [];
        let group=node.visualStateGroups.find(g=>g.name===event.group);
        if(!group) {group={name:event.group,states:[]};node.visualStateGroups.push(group);}
        group.current=event.newState;
      }
    }
  }
  return [...nodes.values()];
}
function parseLiveDump(text,file) {
  let header={},snapshots=new Map(),starts=new Set(),ends=new Map(),endLines=new Map(),trace=[],events=[],warnings=[];
  const lines=text.replace(/^\uFEFF/,'').split(/\r?\n/);
  lines.forEach((line,index)=>{
    if(!line.trim()) return;
    let row;
    try { row=JSON.parse(line); } catch(error) {
      if(lines.slice(index+1).some(s=>s.trim())) throw new Error('Malformed JSON on line '+(index+1));
      warnings.push('Ignored incomplete final line'); return;
    }
    if(row.event==='header') { if(header.schema) throw new Error('Multiple sessions: split the file at its headers'); header=row; }
    if(row.event==='snapshotBegin') starts.add(row.sequence);
    if(row.event==='snapshot') { if(!snapshots.has(row.sequence)) snapshots.set(row.sequence,[]); snapshots.get(row.sequence).push(row); }
    if(row.event==='snapshotEnd') { ends.set(row.sequence,row); endLines.set(row.sequence,index); }
    if(row.event==='interaction') trace.push(row);
    if(liveEventNames.has(row.event)) events.push({index,row});
  });
  const valid=[...snapshots.keys()].filter(s=>starts.has(s)&&ends.get(s)?.captured===snapshots.get(s).length).sort((a,b)=>b-a);
  if(!valid.length) throw new Error('No complete snapshot found');
  const sequence=valid[0], end=ends.get(sequence), nodes=snapshots.get(sequence);
  const liveEvents=header.schema>=5 ? events.filter(e=>e.index>endLines.get(sequence)).map(e=>e.row) : [];
  if(header.schema<4) warnings.push('Schema 3 omits brush opacity, evaluated bindings and current sibling indices');
  if(sequence<Math.max(...snapshots.keys())) warnings.push('A later snapshot is incomplete; using the last complete one');
  if(end.failures) warnings.push('Snapshot reports '+end.failures+' element failures');
  for(const [kind,message] of [['outputGap','Dropped records: continuous coverage is incomplete'],
    ['monitorError','Some elements could not be monitored'],['captureStopped','Capture reached its file-size limit']])
    if(events.some(e=>e.row.event===kind)) warnings.push(message);
  return {file,header,sequence,end,nodes,baselineNodes:nodes,baselineMs:end.elapsedMs||0,liveEvents,trace:[...trace,...liveEvents],warnings};
}
if(typeof module!=='undefined') module.exports={replayNodes,liveEventNames,parseLiveDump};
