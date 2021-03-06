`include "dport.vh"

module regs(
	input wire clk,
	
	input wire [31:0] armaddr,
	output reg [31:0] armrdata,
	input wire [31:0] armwdata,
	input wire armwr,
	input wire armreq,
	output reg armack,
	input wire [3:0] armwstrb,
	output reg armerr,
	
	output reg [19:0] auxaddr,
	output reg [7:0] auxwdata,
	output reg auxreq,
	output reg auxwr,
	input wire auxack,
	input wire auxerr,
	input wire [7:0] auxrdata,
	
	output reg [15:0] debugaddr,
	output reg debugreq,
	input wire debugack,
	input wire [31:0] debugrdata,
	output reg [31:0] debugwdata,
	output reg debugwr,
	
	output reg [5:0] curaddr,
	output reg [31:0] curwdata,
	output reg curreq,
	output reg [3:0] curwstrb,
	
	output reg [`ATTRMAX:0] attr,
	
	output reg [31:0] addrstart,
	output reg [31:0] addrend,
	output reg [31:0] curreg,
	
	output reg [31:0] phyctl,
	input wire [31:0] physts,
	output reg mode16
);

	reg armreq0;
	reg [1:0] state;
	localparam IDLE = 0;
	localparam AUX = 1;
	localparam DEBUG = 2;

	initial begin
		attr = 0;
		auxreq = 0;
		debugreq = 0;
		curreg = 'h80008000;
	end
	always @(posedge clk) begin
		armack <= 0;
		armreq0 <= armreq;
		curreq <= 0;
		case(state)
		IDLE:
			if(armreq && !armreq0)
				casez(armaddr[23:20])
				0:
					if(armwr) begin
						armack <= 1;
						armerr <= 0;
						casez(armaddr[19:0] & -4)
						'h00: phyctl <= armwdata;
						'h08: addrstart <= armwdata;
						'h0c: addrend <= armwdata;
						'h10: curreg <= armwdata;
						'h14: mode16 <= armwdata[0];
						'h40: attr[31:0] <= armwdata;
						'h44: attr[63:32] <= armwdata;
						'h48: attr[95:64] <= armwdata;
						'h4c: attr[127:96] <= armwdata;
						'h50: attr[143:128] <= armwdata[15:0];
						'h54: attr[167:144] <= armwdata[23:0];
						'h58: attr[191:168] <= armwdata[23:0];
						'h5c: attr[208:192] <= armwdata[16:0];
						'h60: attr[232:209] <= armwdata[23:0];
						'h64: attr[256:233] <= armwdata[23:0];
						'h68: attr[273:257] <= armwdata[16:0];
						'b10zz_zzzz: begin
							curaddr <= armaddr[5:0];
							curreq <= 1;
							curwstrb <= armwstrb;
							curwdata <= armwdata;
						end	
						default: armack <= 0;
						endcase
					end else begin
						armack <= 1;
						armerr <= 0;
						case(armaddr[19:0] & -4)
						'h00: armrdata <= phyctl;
						'h04: armrdata <= physts;
						'h08: armrdata <= addrstart;
						'h0c: armrdata <= addrend;
						default: armack <= 0;
						endcase
					end
				1: begin
					state <= AUX;
					auxaddr <= armaddr[19:0];
					auxwdata <= armwdata[{armaddr[1:0], 3'd0} +: 8];
					auxwr <= armwr;
					auxreq <= 1;
				end
				2: begin
					state <= DEBUG;
					debugaddr <= armaddr[15:0];
					debugreq <= 1;
					debugwdata <= armwdata;
					debugwr <= armwr;
				end
				endcase
		AUX:
			if(auxack) begin
				state <= IDLE;
				auxreq <= 0;
				armack <= 1;
				armerr <= auxerr;
				armrdata <= {auxrdata, auxrdata, auxrdata, auxrdata};
			end
		DEBUG:
			if(debugack) begin
				state <= IDLE;
				debugreq <= 0;
				armack <= 1;
				armrdata <= debugrdata;
				armerr <= 0;
			end
		endcase
	end
endmodule
