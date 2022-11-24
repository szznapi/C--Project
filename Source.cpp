#include<iostream>
#include <cstdlib>
#include <sstream>
#include <Magick++.h>
#include <vector>
#include <numeric>
#include <chrono>
#include <set>
#include <array>
#include <thread>
#include <assert.h>


using namespace std;
using namespace Magick;


class PointT : public std::array<int, 3>
{
public:

	PointT() {}
	PointT(int x, int y, int z) : array{ x,y,z }{}

	//{

	//	(*this)[0] = x;
	//	(*this)[1] = y;
	//	(*this)[2] = z;
	//}
};

class KDTree
{
public:

	KDTree() : TreeRootPtr(nullptr) {};

	KDTree(const vector<PointT>& Points)
	{
		BuildTree(Points); 
	}

	~KDTree()
	{
		ClearTree();
	}

	void BuildTree(const vector<PointT>& Points)
	{
		ClearTree();

		TreePoints = Points;
		vector<int> Indices(Points.size());
		iota(begin(Indices), end(Indices), 0); //Fills the range[first, last) with sequentially increasing values

		TreeRootPtr = BuildRecursive(Indices.data(), (int)Points.size(), 0);
	}

	void ClearTree()
	{
		ClearRecursive(TreeRootPtr);
		TreeRootPtr = nullptr;
		TreePoints.clear();
	}

	int nnSearch(const PointT& Query, int* MinDist = nullptr) const
	{
		int Guess;
		int MinDistLoc = numeric_limits<int>::max();

		nnSearchRecursive(Query, TreeRootPtr, &Guess, &MinDistLoc);

		if (MinDist)
			*MinDist = MinDistLoc;

			return Guess;
		
	}

private:

	struct Node
	{
		int Idx; // node index in tree
		Node* NextNodes[2]; // pointer to childrens
		int Axis; // slashed dimention

		Node() : Idx(-1), Axis(-1) { NextNodes[0] = NextNodes[1] = nullptr; }
	};
	
	template <class T, class Compare = less<T>>
	class BoundedPriorityQueue
	{
	public:

		BoundedPriorityQueue() = delete;
		BoundedPriorityQueue(size_t Bound) : InnerBounds(Bound) { InnerElements.reserve(Bound + 1); };

		void push(const T& Val)
		{
			auto it = find_if(begin(InnerElements), end(InnerElements),
				[&](const T& Element) { return Compare()(Val, Element); });
			InnerElements.insert(it, Val);

			if (InnerElements.size() > InnerBounds)
				InnerElements.resize(InnerBounds);
		}

		const T& back() const { return InnerElements.back(); };
		const T& operator[](size_t Index) const { return InnerElements[Index]; }
		size_t size() const { return InnerElements.size(); }

	private:
		size_t InnerBounds;
		std::vector<T> InnerElements;
	};

	using KnnQueue = BoundedPriorityQueue<pair<int, int>>;

	Node* BuildRecursive(int* Indices, int Npoints, int Depth)
	{
		if (Npoints <= 0) return nullptr;

		const int Axis = Depth % 3;
		const int Mid = (Npoints - 1) / 2;

		nth_element(Indices, Indices + Mid, Indices + Npoints, [&](int lhs, int rhs)
			{
				return TreePoints[lhs][Axis] < TreePoints[rhs][Axis];
			});

		Node* NodePtr = new Node();
		NodePtr->Idx = Indices[Mid];
		NodePtr->Axis = Axis;

		NodePtr->NextNodes[0] = BuildRecursive(Indices, Mid, Depth + 1);
		NodePtr->NextNodes[1] = BuildRecursive(Indices + Mid + 1, Npoints - Mid - 1, Depth + 1);

		return NodePtr;
	}

	void ClearRecursive(Node* Node)
	{
		if (Node == nullptr)
			return;

		if (Node->NextNodes[0])
			ClearRecursive(Node->NextNodes[0]);

		if (Node->NextNodes[1])
			ClearRecursive(Node->NextNodes[1]);

		delete Node;
	}

	static int distance(const PointT& P, const PointT& Q)
	{
		int Dist = 0; // d = ((x2 - x1)2 + (y2 - y1)2 + (z2 - z1)2)1/2  

		for (size_t i = 0; i <3; ++i)
			Dist += (P[i] - Q[i]) * (P[i] - Q[i]);
		return sqrt(Dist);
	}

	void nnSearchRecursive(const PointT& Query, const Node* Node, int* Guess, int* MinDist) const
	{
		if (Node == nullptr)
			return;

		const PointT& train = TreePoints[Node->Idx];

		const int Dist = distance(Query, train);
		if (Dist < *MinDist)
		{
			*MinDist = Dist;
			*Guess = Node->Idx;
		}

		const int Axis = Node->Axis;
		const int Dir = Query[Axis] < train[Axis] ? 0 : 1;
		nnSearchRecursive(Query, Node->NextNodes[Dir], Guess, MinDist);

		const int Diff = fabs(Query[Axis] - train[Axis]);
		if (Diff < *MinDist)
			nnSearchRecursive(Query, Node->NextNodes[!Dir], Guess, MinDist);
	}

	Node* TreeRootPtr;             
	std::vector<PointT> TreePoints;
};


int main()
{

	
	int ImageAColors, ImageBColors, DurationSec;

		
	Image SourceImage;
	Image Pallete;
	
	
	SourceImage.read("obraz-A.jpg"); // load test images
	Pallete.read("obraz-B.jpg");

	Image OutputImage(SourceImage);



	set<PointT> uniqueSourceCollors;

	std::thread ColourBCounterThread([&](){
		for (int i = 0; i < SourceImage.columns(); ++i)// get all unique rgb values of source image
		{
			for (int j = 0; j < SourceImage.rows(); ++j)
			{
				Color c = SourceImage.pixelColor(i, j);
				uniqueSourceCollors.insert(PointT(c.quantumRed(), c.quantumGreen(), c.quantumBlue()));
			}
		}
		});
	
	set<PointT> uniqueCollors;

	auto start = chrono::steady_clock::now();

	


	for (int i = 0; i < Pallete.columns(); ++i)// construct collor pallete = unique rgb values
	{
		for (int j = 0; j < Pallete.rows(); ++j)
		{
			Color c = Pallete.pixelColor(i, j);
			uniqueCollors.insert(PointT(c.quantumRed(), c.quantumGreen(), c.quantumBlue()));

		}
	}
	ImageAColors = uniqueCollors.size();
	
	vector<PointT> CollorsPallete(uniqueCollors.begin(), uniqueCollors.end());



	// create kdTree 
	KDTree  kdTree(CollorsPallete);


	for (int i = 0; i < SourceImage.columns(); ++i)
	{
		for (int j = 0; j < SourceImage.rows(); ++j)
		{
			Color c = SourceImage.pixelColor(i, j);
			const PointT query(c.quantumRed(), c.quantumGreen(), c.quantumBlue());

			PointT  closest = CollorsPallete[kdTree.nnSearch(query)]; 
			OutputImage.pixelColor(i, j, Color(closest[0], closest[1], closest[2]));
		}
	}

	OutputImage.write("Image-C.png");

	ColourBCounterThread.join();

	ImageBColors = uniqueSourceCollors.size();


auto end = chrono::steady_clock::now();

	cout << " time in seconds: "<< chrono::duration_cast<chrono::seconds>(end - start).count()<< " sec";
	cout << "First image pallete : " << ImageAColors << " colours." << endl;
	cout << "Second image pallete : " << ImageBColors << " colours." << endl;


	return 0;
}