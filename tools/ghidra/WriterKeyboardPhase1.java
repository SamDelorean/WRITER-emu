//@category Writer
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class WriterKeyboardPhase1 extends GhidraScript {
  private int u(int a) throws Exception { return currentProgram.getMemory().getByte(toAddr(a)) & 0xff; }
  private void lab(long a,String s) throws Exception { createLabel(toAddr(a),s,true); }
  private void comment(long a,String s) throws Exception {
    currentProgram.getListing().setComment(toAddr(a),CodeUnit.PRE_COMMENT,s);
  }
  private String bytes(int a,int n) throws Exception {
    StringBuilder b=new StringBuilder();
    for(int i=0;i<n;i++){if(i>0)b.append(' ');b.append(String.format("%02X",u(a+i)));}
    return b.toString();
  }
  public void run() throws Exception {
    String out=getScriptArgs()[0]; new File(out).mkdirs();
    lab(0x0B64,"KEY_MODIFIER_STATE_SAMPLE_5BITS");
    lab(0x0BC3,"KEY_SCAN_FILTER_MASKS");
    lab(0x0BD3,"KEY_SCAN_BIT_TO_INDEX");
    lab(0x0BE2,"KEY_SCAN_MATRIX_8_ROWS");
    lab(0x0C22,"KEY_FIFO_ENQUEUE_2BYTE_EVENT");
    lab(0x0C40,"KEY_DEBOUNCE_AND_REPEAT");
    lab(0x0CBE,"KEY_FIFO_DEQUEUE");
    lab(0x0CF6,"KEY_MATRIX_SNAPSHOT_COMPARE_16B");
    lab(0x0D20,"KEY_SET_PENDING_RESULT");
    lab(0x0D55,"KEY_EVENT_DISPATCH");
    lab(0x0D68,"KEY_HANDLER_NOOP");
    lab(0x0D94,"KEY_HANDLER_CONTROL");
    lab(0x0D9A,"KEY_HANDLER_SPECIAL");
    lab(0x0DA4,"KEY_HANDLER_SYMBOL");
    lab(0x0DBC,"KEY_HANDLER_ALPHA");
    lab(0x23A5,"MASTER_RESET_EXPECTED_PASSWORD_ACE135");
    lab(0x23AC,"MASTER_RESET_MATRIX_PATTERN_A");
    lab(0x23BC,"MASTER_RESET_MATRIX_PATTERN_B");
    lab(0x23DC,"MASTER_RESET_MATRIX_PATTERN_C");
    lab(0x2460,"MASTER_RESET_KEYBOARD_INPUT_LOOP");
    lab(0x2523,"MASTER_RESET_PASSWORD_ACCEPTED");
    comment(0x0B64,"Samples five dedicated matrix positions and returns their state in A bits 0..4. Called by debounce/event generation at 0x0C6C.");
    comment(0x0CF6,"Compares a 16-byte matrix snapshot: for each of 8 row strobes, reads port 0x82 then 0x80 and compares both bytes against [HL]. HL=0 selects the all-zero pattern at 0x0CE6. Z means exact match.");
    comment(0x23AC,"16-byte raw matrix snapshot pattern A. Row pairs are ordered strobe 0x80..0x01; each pair is port 0x82 then 0x80.");
    comment(0x23BC,"16-byte raw matrix snapshot pattern B. Row pairs are ordered strobe 0x80..0x01; each pair is port 0x82 then 0x80.");
    comment(0x23DC,"16-byte raw matrix snapshot pattern C. Row pairs are ordered strobe 0x80..0x01; each pair is port 0x82 then 0x80.");
    long[] code={0x0B64,0x0BD3,0x0BE2,0x0C22,0x0C40,0x0CBE,0x0CF6,0x0D20,0x0D55,0x0D68,0x0D8C,0x0D94,0x0D9A,0x0DA4,0x0DBC,0x2460,0x2523,0x267D,0x2682,0x278B,0x27C1};
    for(long a:code){try{disassemble(toAddr(a));}catch(Exception e){}}
    PrintWriter p=new PrintWriter(new FileWriter(out+"/modifier_and_reset_patterns.tsv"));
    p.println("pattern\trow\tstrobe\tport82_expected\tport80_expected");
    for(String[] q:new String[][]{{"A","23AC"},{"B","23BC"},{"C","23DC"}}){
      int a=Integer.parseInt(q[1],16);
      for(int row=0;row<8;row++)
        p.printf("%s\t%d\t%02X\t%02X\t%02X%n",q[0],row,0x80>>row,u(a+2*row),u(a+2*row+1));
    }
    p.close();
    PrintWriter m=new PrintWriter(new FileWriter(out+"/modifier_key_slots.tsv"));
    m.println("modifier_bit\tstrobe\tport\tinput_bit\tderived_table_index\ttable_handler\tentry_data");
    int[][] v={{0,0x20,0x82,1,0x26},{1,0x04,0x82,1,0x56},{2,0x02,0x80,1,0x6E},{3,0x10,0x80,7,0x38},{4,0x08,0x82,7,0x40}};
    for(int[] z:v){int a=0x0964+4*z[4];int h=u(a)|(u(a+1)<<8);
      m.printf("%d\t%02X\t%02X\t%d\t%02X\t%04X\t%02X/%02X%n",z[0],z[1],z[2],z[3],z[4],h,u(a+2),u(a+3));}
    m.close();
    PrintWriter d=new PrintWriter(new FileWriter(out+"/phase1_disassembly.txt"));
    for(long[] r:new long[][]{{0x0B64,0x0BC3},{0x0BD3,0x0C22},{0x0C22,0x0C40},{0x0C40,0x0CBE},{0x0CBE,0x0CE6},{0x0CF6,0x0D24},{0x0D55,0x0E08},{0x2450,0x24A8},{0x24B5,0x24E8},{0x2523,0x256C},{0x278B,0x27D6}}){
      d.printf("\n### %04X-%04X%n",r[0],r[1]);int a=(int)r[0];
      while(a<r[1]){Instruction ins=currentProgram.getListing().getInstructionAt(toAddr(a));
        if(ins==null){d.printf("%04X: %02X [not disassembled]%n",a,u(a));a++;}
        else {d.printf("%s: %s %s%n",ins.getAddress(),bytes(a,(int)ins.getLength()),ins.toString());a+=(int)ins.getLength();}}
    }
    d.close();
    println("WRITER_KEYBOARD_PHASE1_OK modifiers=5 reset_patterns=3 output="+out);
  }
}