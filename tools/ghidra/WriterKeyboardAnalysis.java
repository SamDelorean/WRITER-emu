//@category Writer
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class WriterKeyboardAnalysis extends GhidraScript {
  private int u(int a) throws Exception { return currentProgram.getMemory().getByte(toAddr(a)) & 0xff; }
  private String ch(int v) { return (v >= 0x20 && v < 0x7f) ? Character.toString((char)v) : "."; }
  private String hex(Instruction ins) throws Exception {
    StringBuilder s=new StringBuilder();
    for(byte b:ins.getBytes()){ if(s.length()>0)s.append(' '); s.append(String.format("%02X",b&255)); }
    return s.toString();
  }
  private void label(long a, String s) throws Exception { createLabel(toAddr(a), s, true); }
  public void run() throws Exception {
    String out = getScriptArgs()[0]; new File(out).mkdirs();
    label(0x0964,"KEYMAP_128_ENTRIES_STRIDE4");
    label(0x0B64,"KEY_READ_MODIFIERS"); label(0x0BD3,"KEY_MATRIX_INDEX_RESOLVE");
    label(0x0BE2,"KEY_MATRIX_SCAN"); label(0x0C22,"KEY_EVENT_FIFO_ENQUEUE");
    label(0x0C40,"KEY_DEBOUNCE_REPEAT"); label(0x0D55,"KEY_EVENT_DISPATCH");
    label(0x0D68,"KEY_HANDLER_NOOP"); label(0x0D8C,"KEY_HANDLER_TOGGLE_STATE");
    label(0x0D94,"KEY_HANDLER_CONTROL"); label(0x0D9A,"KEY_HANDLER_SPECIAL");
    label(0x0DA4,"KEY_HANDLER_SYMBOL"); label(0x0DBC,"KEY_HANDLER_ALPHA");
    long[] entries={0x0B64,0x0BD3,0x0BE2,0x0C22,0x0C40,0x0D55,0x0D68,0x0D8C,0x0D94,0x0D9A,0x0DA4,0x0DBC};
    for(long a:entries){ try { disassemble(toAddr(a)); } catch(Exception e){} }
    PrintWriter t=new PrintWriter(new FileWriter(out+"/keyboard_table_0964.tsv"));
    t.println("index\trom_offset\thandler\tdata0_hex\tdata1_hex\tbase\tshifted");
    for(int i=0;i<128;i++){ int a=0x0964+4*i,h=u(a)|(u(a+1)<<8),x=u(a+2),y=u(a+3);
      t.printf("%02X\t%04X\t%04X\t%02X\t%02X\t%s\t%s%n",i,a,h,x,y,ch(x),ch(y)); }
    t.close();
    PrintWriter d=new PrintWriter(new FileWriter(out+"/keyboard_routines_disassembly.txt"));
    for(long[] range:new long[][]{{0x0B64,0x0BC3},{0x0BD3,0x0C22},{0x0C22,0x0C40},{0x0C40,0x0CBE},{0x0D55,0x0E08}}){
      d.printf("\n### %04X-%04X%n",range[0],range[1]); int a=(int)range[0];
      while(a<range[1]){ Instruction ins=currentProgram.getListing().getInstructionAt(toAddr(a));
        if(ins==null){ d.printf("%04X: %02X  [not disassembled]%n",a,u(a)); a++; }
        else { d.printf("%s: %-18s %s%n",ins.getAddress(),hex(ins),ins.toString()); a+=(int)ins.getLength(); } }
    }
    d.close();
    println("KEYBOARD_ANALYSIS_OK table_entries=128 stride=4 handlers=0D68,0D8C,0D94,0D9A,0DA4,0DBC output="+out);
  }
}