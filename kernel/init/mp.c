#include <mp.h>
#include <stdio.h>

#include <cpu.h>

static uint8_t sum(uint8_t *addr, int len)
{
  int i, sum;

  sum = 0;
  for(i=0; i<len; i++)
    sum += addr[i];
  return sum;
}

// Look for an MP structure in the len bytes at addr.
static struct mp* mpsearch1(uint a, int len)
{
  uint8_t *e, *p, *addr;

  addr = (uint8_t*)a;
  e = addr+len;
  for(p = addr; p < e; p += sizeof(struct mp))
    if(memcmp(p, "_MP_", 4) == 0 && sum(p, sizeof(struct mp)) == 0)
      return (struct mp*)p;
  return 0;
}

// Search for the MP Floating Pointer Structure, which according to the
// spec is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct mp* mpsearch(void)
{
  uint8_t *bda;
  uint p;
  struct mp *mp;

  bda = (uint8_t *) 0x400;
  if((p = ((bda[0x0F]<<8)| bda[0x0E]) << 4)){
    if((mp = mpsearch1(p, 1024)))
      return mp;
  } else {
    p = ((bda[0x14]<<8)|bda[0x13])*1024;
    if((mp = mpsearch1(p-1024, 1024)))
      return mp;
  }
  return mpsearch1(0xF0000, 0x10000);
}

// Search for an MP configuration table.  For now,
// don't accept the default configurations (physaddr == 0).
// Check for correct signature, calculate the checksum and,
// if correct, check the version.
// To do: check extended table checksum.
static struct mpconf* mpconfig(struct mp **pmp)
{
  struct mpconf *conf;
  struct mp *mp;

  if((mp = mpsearch()) == 0 || mp->physaddr == 0)
    return 0;
  conf = (struct mpconf*)mp->physaddr;
  if(memcmp(conf, "PCMP", 4) != 0)
    return 0;
  if(conf->version != 1 && conf->version != 4)
    return 0;
  if(sum((uint8_t*)conf, conf->length) != 0)
    return 0;
  *pmp = mp;
  return conf;
}

extern uint *lapic;


struct mpbus bus_array[0x20];
int nbus;
struct mpinst inst_array[0x20];
int ninst;

void mpinit(){
  struct mp *mp;
  struct mpconf *conf;
  conf = mpconfig(&mp);
  if(conf == 0){
    return;
  }

  lapic = (uint*)conf->lapicaddr;
  debug("lapic address: 0x%08x\n",lapic);
  uint8_t* ptr = (uint8_t*)&conf[1];
  debug("conf entry: %d\n",conf->entry);
  for(int i=0;i<conf->entry;i++){
      switch(*ptr){
        case MPPROC:
          struct mpproc* proc = (struct mpproc*)ptr;
          cpus[ncpu].apicid = proc->apicid;
          ncpu++;
          ptr += sizeof(struct mpproc);
          break;
        case MPIOAPIC:
          // struct mpioapic* ioapic = (struct mpioapic*)ptr;
          // uint8_t ioapicid = ioapic->apicno;
          ptr += sizeof(struct mpioapic);
          break;
        case MPBUS:
         memcpy(&bus_array[nbus],ptr,sizeof(struct mpbus));
         debug("find bus entry: %s\n",bus_array[nbus].name);
         bus_array[nbus].name[5] = 0;
         nbus++;
         ptr += sizeof(struct mpbus);
         break;
        case MPIOINTR:
         memcpy(&inst_array[ninst],ptr,sizeof(struct mpinst));
         debug("find inst bus_id: %d pin: %d dev_num: %d apic_id: %d apic_intr: %d\n",
            inst_array[ninst].src_bus_id,
            inst_array[ninst].src_bus_irq&0b11,
            inst_array[ninst].src_bus_irq>>2,
            inst_array[ninst].dst_apic_id,
            inst_array[ninst].dst_apic_intin
          );
         ninst++;
         ptr += sizeof(struct mpinst);
         break;
        case MPLINTR:
         // debug("MPLINTR entry\n");
          ptr += 8;
          break;
        default:
          debug("wtf entry");
          break;
      }
  } 
}



int getIRQPin(char* bus,int irq){
  int bus_id = -1;
  for(int i=0;i<nbus;i++){
    if(!memcmp(bus_array[i].name,bus,4)){
      bus_id = bus_array[i].bus_id;
      break;
    }
  }
  if(bus_id == -1){
    debug("can't find bus\n");
    return -1;
  }
  for(int i=0;i<ninst;i++){
    if(inst_array[i].src_bus_id == bus_id && inst_array[i].src_bus_irq == irq){
      return inst_array[i].dst_apic_intin;
    }
  }
  debug("can't find apic pin\n");
  return -1;
}



