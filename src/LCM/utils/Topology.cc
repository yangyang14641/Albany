/*
 * Topology.cc
 *  Set of topology manipulation functions for 2D and 3D stk meshes.
 *  Created on: Jul 11, 2011
 *      Author: jrthune
 */

#include "Topology.h"

namespace LCM{

/*
 * Default constructor for topology
 */
topology::topology():
		numDim(0),
		discretization_ptr_(Teuchos::null)
{
	return;
}

/*
 * Create mesh data structure
 *   Inputs:
 *     input_file is exodus II input file name
 *     output_file is exodus II output file name
 */
topology::topology(
		std::string const & input_file,
		std::string const & output_file)
{
	Teuchos::RCP<Teuchos::ParameterList>
	  disc_params = rcp(new Teuchos::ParameterList("params"));

	//set Method to Exodus and set input file name
	disc_params->set<std::string>("Method", "Exodus");
	disc_params->set<std::string>("Exodus Input File Name", input_file);
	disc_params->set<std::string>("Exodus Output File Name", output_file);
	//disc_params->print(std::cout);

	Teuchos::RCP<Epetra_Comm>
	  communicator = Albany::createEpetraCommFromMpiComm(Albany_MPI_COMM_WORLD);

	Albany::DiscretizationFactory
	  disc_factory(disc_params, communicator);

	const Teuchos::RCP<Albany::MeshSpecsStruct>
	meshSpecs = disc_factory.createMeshSpecs();

	Teuchos::RCP<Albany::StateInfoStruct>
	stateInfo = Teuchos::rcp(new Albany::StateInfoStruct());

	discretization_ptr_ = disc_factory.createDiscretization(3, stateInfo);

	Teuchos::ArrayRCP<double>
	  coordinates = discretization_ptr_->getCoordinates();

	// Need to access the bulkData and metaData classes in the mesh datastructure
	Albany::STKDiscretization &
	  stk_discretization = static_cast<Albany::STKDiscretization &>(*discretization_ptr_);

	stkMeshStruct_ = stk_discretization.getSTKMeshStruct();

	bulkData_ = stkMeshStruct_->bulkData;
	stk::mesh::fem::FEMMetaData & metaData = *stkMeshStruct_->metaData;

	// The entity ranks
	nodeRank = metaData.NODE_RANK;
	edgeRank = metaData.EDGE_RANK;
	faceRank = metaData.FACE_RANK;
	elementRank = metaData.element_rank();
	numDim = stkMeshStruct_->numDim;

	return;
}

/*
 * Output relations associated with entity
 */
void
topology::disp_relation(
		Entity & entity)
{
	cout << "Relations for entity (identifier,rank): " << entity.identifier()
			<< "," << entity.entity_rank() << "\n";
	stk::mesh::PairIterRelation relations = entity.relations();
		for (int i = 0; i < relations.size(); ++i){
			cout << "entity:\t" << relations[i].entity()->identifier() << ","
				 << relations[i].entity()->entity_rank() << "\tlocal id: "
				 << relations[i].identifier() << "\n";
		}
		return;
}

/*
 * Output relations of rank entityRank associated with entity
 */
void
topology::disp_relation(
		Entity & entity,
		EntityRank entityRank)
{
	cout << "Relations of rank " << entityRank << " for entity (identifier,rank): "
			<< entity.identifier() << "," << entity.entity_rank() << "\n";
	stk::mesh::PairIterRelation relations = entity.relations(entityRank);
		for (int i = 0; i < relations.size(); ++i){
			cout << "entity:\t" << relations[i].entity()->identifier() << ","
				 << relations[i].entity()->entity_rank() << "\tlocal id: "
				 << relations[i].identifier() << "\n";
		}
		return;
}

/*
 * Output the mesh connectivity
 *   stk mesh must contain relations between the elements and the nodes (as in
 *   the original stk mesh object)
 */
void
topology::disp_connectivity()
{
	// Create a list of element entities
	std::vector<Entity*> element_lst;
	stk::mesh::get_entities(*(bulkData_),elementRank,element_lst);

	// Loop over the elements
	for (int i = 0; i < element_lst.size(); ++i){
		stk::mesh::PairIterRelation relations = element_lst[i]->relations(nodeRank);
		cout << "Nodes of Element " << element_lst[i]->identifier() << "\n";

		for (int j = 0; j < relations.size(); ++j){
			Entity& node = *(relations[j].entity());
			cout << ":"  << node.identifier();
		}
		cout << ":\n";
	}

	//topology::disp_relation(*(element_lst[0]));

	//std::vector<Entity*> face_lst;
	//stk::mesh::get_entities(*(bulkData_),elementRank-1,face_lst);
	//topology::disp_relation(*(face_lst[1]));

	return;
}

/*
 * Generic fracture criterion function. Given an entity and
 *   probability, will determine if criterion is met.
 *   Will return true if fracture criterion is met, else false.
 *   Fracture only defined on surface of elements. Thus, input entity
 *   must be of rank dimension-1, else error.
 *
 *   Fracture only defined if entity is boundary of two elements.
 */
bool
topology::fracture_criterion(
		Entity& entity,
		float p)
{
	// Fracture only defined on the boundary of the elements
	EntityRank rank = entity.entity_rank();
	assert(rank==numDim-1);

	stk::mesh::PairIterRelation relations = entity.relations(elementRank);
	if(relations.size()==1)
		return false;

	bool is_open = false;
	// Check criterion
	float random = 0.5 + 0.5*Teuchos::ScalarTraits<double>::random();
	if (random < p)
		is_open = true;

	return is_open;
}

/*
 * Iterates over the boundary entities of the mesh of (all entities of rank
 *   dimension-1) and checks fracture criterion. If fracture_criterion is
 *   met, the entity and all lower order entities associated with it are
 *   marked as open.
 */
void
topology::set_entities_open(
		std::map<EntityKey,bool>& entity_open)
{
	// Fracture occurs at the boundary of the elements in the mesh.
	//   The rank of the boundary elements is one less than the
	//   dimension of the system.
	std::vector<Entity*> boundary_lst;
	stk::mesh::get_entities(*(bulkData_),numDim-1,boundary_lst);

	// Probability that fracture_criterion will return true.
	float p = 0.5;

	// Iterate over the boundary entities
	for (int i = 0; i < boundary_lst.size(); ++i){
		Entity& entity = *(boundary_lst[i]);
		bool is_open = topology::fracture_criterion(entity,p);
		// If the criterion is met, need to set lower rank entities
		//   open as well
		if (is_open == true && numDim == 3){
			entity_open[entity.key()] = true;
			stk::mesh::PairIterRelation segments =
					entity.relations(entity.entity_rank()-1);
			// iterate over the segments
			for (int j = 0; j < segments.size(); ++j){
				Entity & segment = *(segments[j].entity());
				entity_open[segment.key()] = true;
				stk::mesh::PairIterRelation nodes =
						segment.relations(segment.entity_rank()-1);
				// iterate over nodes
				for (int k = 0; k < nodes.size(); ++k){
					Entity& node = *(nodes[k].entity());
					entity_open[node.key()] = true;
				}
			}
		}
		// If the mesh is 2D
		else if (is_open == true && numDim == 2){
			entity_open[entity.key()] = true;
			stk::mesh::PairIterRelation nodes =
					entity.relations(entity.entity_rank()-1);
			// iterate over nodes
			for (int j = 0; j < nodes.size(); ++j){
				Entity & node = *(nodes[j].entity());
				entity_open[node.key()] = true;
			}
		}
	}

	return;
}

/*
 * Output the graph associated with the mesh to graphviz .dot
 *   file for visualization purposes. If fracture criterion for
 *   entity is satisfied, the entity and all associated lower order
 *   entities are marked open. All open entities are displayed as
 *   such in output file.
 * Can create figure using:
 *   dot -Tpng output.dot -o output.png
 */
void
topology::output_to_graphviz(
		std::string & gviz_output,
		std::map<EntityKey, bool> & entity_open)
{
	// Open output file
	std::ofstream gviz_out;
	gviz_out.open (gviz_output.c_str(), std::ios::out);

	cout << "Write graph to graphviz dot file\n";

	if (gviz_out.is_open()){
		// Write beginning of file
		gviz_out << "digraph mesh {\n"
				 << "  node [colorscheme=paired12]\n"
				 << "  edge [colorscheme=paired12]\n";

		std::vector<Entity*> entity_lst;
		stk::mesh::get_entities(*(bulkData_),elementRank,entity_lst);

		std::vector<std::vector<Entity*> > relation_lst;
		std::vector<int> relation_local_id;

		// Elements
		for (int i = 0; i < entity_lst.size(); ++i){
			Entity & entity = *(entity_lst[i]);
			stk::mesh::PairIterRelation relations = entity.relations();

			gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					 << "\" [label=\"Element " << entity.identifier()
					 << "\",style=filled,fillcolor=\"8\"]\n";
			for (int j = 0; j < relations.size(); ++j){
				if (relations[j].entity_rank() < entity.entity_rank()){
					std::vector<Entity*> temp;
					temp.push_back(&entity);
					temp.push_back(relations[j].entity());
					relation_lst.push_back(temp);
					relation_local_id.push_back(relations[j].identifier());
				}
			}
		}

		stk::mesh::get_entities(*(bulkData_),faceRank,entity_lst);

		// Faces
		for (int i = 0; i < entity_lst.size(); ++i){
			Entity & entity = *(entity_lst[i]);
			stk::mesh::PairIterRelation relations = entity.relations();

			if (entity_open[entity.key()] == true)
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Face " << entity.identifier()
					<< "\",style=filled,fillcolor=\"1\"]\n";
			else
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Face " << entity.identifier()
					<< "\",style=filled,fillcolor=\"2\"]\n";
			for (int j = 0; j < relations.size(); ++j){
				if (relations[j].entity_rank() < entity.entity_rank()){
					std::vector<Entity*> temp;
					temp.push_back(&entity);
					temp.push_back(relations[j].entity());
					relation_lst.push_back(temp);
					relation_local_id.push_back(relations[j].identifier());
				}
			}
		}

		stk::mesh::get_entities(*(bulkData_),edgeRank,entity_lst);

		// Edges
		for (int i = 0; i < entity_lst.size(); ++i){
			Entity & entity = *(entity_lst[i]);
			stk::mesh::PairIterRelation relations = entity.relations();

			if (entity_open[entity.key()] == true)
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Segment " << entity.identifier()
					<< "\",style=filled,fillcolor=\"3\"]\n";
			else
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Segment " << entity.identifier()
					<< "\",style=filled,fillcolor=\"4\"]\n";
			for (int j = 0; j < relations.size(); ++j){
				if (relations[j].entity_rank() < entity.entity_rank()){
					std::vector<Entity*> temp;
					temp.push_back(&entity);
					temp.push_back(relations[j].entity());
					relation_lst.push_back(temp);
					relation_local_id.push_back(relations[j].identifier());
				}
			}
		}

		stk::mesh::get_entities(*(bulkData_),nodeRank,entity_lst);

		// Nodes
		for (int i = 0; i < entity_lst.size(); ++i){
			Entity & entity = *(entity_lst[i]);

			if (entity_open[entity.key()] == true)
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Node " << entity.identifier()
					<< "\",style=filled,fillcolor=\"5\"]\n";
			else
				gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
					<< "\" [label=\"Node " << entity.identifier()
					<< "\",style=filled,fillcolor=\"6\"]\n";
		}

		for (int i = 0; i < relation_lst.size(); ++i){
			std::vector<Entity*> temp = relation_lst[i];
			Entity& origin = *(temp[0]);
			Entity& destination = *(temp[1]);
			std::string color;
			switch(relation_local_id[i]){
			case 0:
				color = "6";
				break;
			case 1:
				color = "4";
				break;
			case 2:
				color = "2";
				break;
			case 3:
				color = "8";
				break;
			case 4:
				color = "10";
				break;
			case 5:
				color = "12";
				break;
			default:
				color = "9";
			}
			gviz_out << "  \"" << origin.identifier() << "_" << origin.entity_rank()
					 << "\" -> \"" << destination.identifier() << "_" << destination.entity_rank()
					 << "\" [color=\"" << color << "\"]" << "\n";
		}

		// File end
		gviz_out << "}";
		gviz_out.close();
	}
	else
		cout << "Unable to open graphviz output file 'output.dot'\n";

	return;
}

/*
 * Creates the full graph representation of the mesh. Default graph has only
 *   elements and nodes. The original node connectivity will be deleted in
 *   later steps, store the connectivity in temporary array.
 *
 * Note: Function must be called before mesh modification begins
 */
void
topology::graph_initialization()
{
	stk::mesh::PartVector add_parts;
	stk::mesh::create_adjacent_entities(*(bulkData_), add_parts);

	// Create the temporary connectivity array
	std::vector<Entity*> element_lst;
	stk::mesh::get_entities(*(bulkData_),elementRank,element_lst);

	for (int i = 0; i < element_lst.size(); ++i){
		stk::mesh::PairIterRelation nodes = element_lst[i]->relations(nodeRank);
		std::vector<Entity*> temp;
		for (int j = 0; j < nodes.size(); ++j){
			Entity* node = nodes[j].entity();
			temp.push_back(node);
		}
		connectivity_temp.push_back(temp);
	}

	bulkData_->modification_begin();
	topology::remove_extra_relations();
	bulkData_->modification_end();

	return;
}

/*
 * stk::mesh::create_adjacent_entities creates all entities in
 *   graph instead of default elements and nodes. All entities are
 *   connected through relationships. Graph algorithms require
 *   relationships only between entities separated by one degree, e.g. elements
 *   and faces in a 3D graph.
 *   Function removes all other relationships, e.g. between elements
 *   and nodes.
 *
 * Note: Valid for 2D and 3D meshes.
 */
void
topology::remove_extra_relations()
{
	std::vector<Entity*> element_lst;
	stk::mesh::get_entities(*(bulkData_),elementRank,element_lst);

	// Remove extra relations from element
	for (int i = 0; i < element_lst.size(); ++i){
		Entity & element = *(element_lst[i]);
		stk::mesh::PairIterRelation relations = element.relations();
		std::vector<Entity*> del_relations;
		std::vector<int> del_ids;
		for (stk::mesh::PairIterRelation::iterator j = relations.begin();
				j != relations.end(); ++j){
			if (j->entity_rank() != elementRank-1){
				del_relations.push_back(j->entity());
				del_ids.push_back(j->identifier());
			}
		}
		for (int j = 0; j < del_relations.size(); ++j){
			Entity & entity = *(del_relations[j]);
			bulkData_->destroy_relation(element,entity,del_ids[j]);
		}
	};

	if (elementRank == 3){
		// Remove extra relations from face
		std::vector<Entity*> face_lst;
		stk::mesh::get_entities(*(bulkData_),elementRank-1,face_lst);
		EntityRank entityRank = face_lst[0]->entity_rank();
		for (int i = 0; i < face_lst.size(); ++i){
			Entity & face = *(face_lst[i]);
			stk::mesh::PairIterRelation relations = face_lst[i]->relations();
			std::vector<Entity*> del_relations;
			std::vector<int> del_ids;
			for (stk::mesh::PairIterRelation::iterator j = relations.begin();
					j != relations.end(); ++j){
				if (j->entity_rank() != entityRank+1 &&
						j->entity_rank() != entityRank-1){
					del_relations.push_back(j->entity());
					del_ids.push_back(j->identifier());
				}
			}
			for (int j = 0; j < del_relations.size(); ++j){
				Entity & entity = *(del_relations[j]);
				bulkData_->destroy_relation(face,entity,del_ids[j]);
			}
		}
	}

	return;
}

/*
 * After mesh manipulations are complete, need to recreate the original
 *   mesh representation as expected by Albany_STKDiscretization. Remove
 *   all extra entities (faces and edges for a 3D mesh) and recreate relationships
 *   between elements and nodes. Nodal connectivity data for each element is stored
 *   in connectivity_temp.
 *
 * Note: must be called before mesh modification has ended
 */
void
topology::graph_cleanup()
{
	std::vector<Entity*> element_lst;
	stk::mesh::get_entities(*(bulkData_),elementRank,element_lst);

	// Remove faces from graph
	std::vector<Entity*> face_lst;
	stk::mesh::get_entities(*(bulkData_),faceRank,face_lst);
	for (int i = 0; i < face_lst.size(); ++i){
		//bulkData::destroy_entity() requires entity has no relations
		stk::mesh::PairIterRelation relations = face_lst[i]->relations();
		for (int j = 0; j < relations.size(); ++j){
			// relation must be from higher to lower rank entity
			if (face_lst[i]->entity_rank() > relations[j].entity_rank())
				bulkData_->destroy_relation(*(face_lst[i]),
						*(relations[j].entity()),relations[j].identifier());
			else
				bulkData_->destroy_relation(*(relations[j].entity()),
						*(face_lst[i]),relations[j].identifier());
		}
		bulkData_->destroy_entity(face_lst[i]);
	}

	// Remove edges from graph
	std::vector<Entity*> edge_lst;
	stk::mesh::get_entities(*(bulkData_),edgeRank,edge_lst);
	for (int i = 0; i < edge_lst.size(); ++i){
		//bulkData::destroy_entity() requires entity has no relations
		stk::mesh::PairIterRelation relations = edge_lst[i]->relations();
		for (int j = 0; j < relations.size(); ++j){
			// relation must be from higher to lower rank entity
			if (edge_lst[i]->entity_rank() > relations[j].entity_rank())
				bulkData_->destroy_relation(*(edge_lst[i]),
						*(relations[j].entity()),relations[j].identifier());
			else
				bulkData_->destroy_relation(*(relations[j].entity()),
						*(edge_lst[i]),relations[j].identifier());
		}
		bulkData_->destroy_entity(edge_lst[i]);
	}

	// Add relations from element to nodes
	for (int i = 0; i < element_lst.size(); ++i){
		Entity & element = *(element_lst[i]);
		for (int j = 0; j < connectivity_temp.size(); ++j){
			Entity & node = *(connectivity_temp[i][j]);
			bulkData_->declare_relation(element,node,j);
		}
	}

	return;
}

/*
 * Create vectors describing the vertices and edges of the star of an entity
 *   in the stk mesh.
 *
 *   The star of a graph vertex is defined as the vertex and all higher order
 *   vertices which are connected to it when traversing up the graph from the
 *   input vertex.
 *
 *   Valid for entities of all ranks
 */
void
topology::star(std::set<EntityKey> & subgraph_entity_lst,
		std::set<stkEdge,EdgeLessThan> & subgraph_edge_lst,
		Entity & entity)
{
	stk::mesh::PairIterRelation relations = entity.relations(entity.entity_rank()+1);
	subgraph_entity_lst.insert(entity.key());
	for (stk::mesh::PairIterRelation::iterator i = relations.begin();
			i != relations.end(); ++i){
		stk::mesh::Relation relation = *i;
		Entity & source = *(relation.entity());
		stkEdge edge;
		edge.source = source.key();
		edge.target = entity.key();
		edge.localId = relation.identifier();
		subgraph_edge_lst.insert(edge);
		topology::star(subgraph_entity_lst,subgraph_edge_lst,source);
	}

	return;
}

/*
 * Fractures all open boundary entities of the mesh.
 */
void
topology::fracture_boundary(std::map<EntityKey, bool> & entity_open){

	// Get set of open nodes
	std::vector<Entity*> node_lst; //all nodes
	std::vector<Entity*> open_node_lst; //only the open nodes
	stk::mesh::get_entities(*bulkData_,nodeRank,node_lst);

	for(std::vector<Entity*>::iterator i = node_lst.begin();
			i != node_lst.end(); ++i){
		Entity* entity = *i;
		if (entity_open[entity->key()]==true){
			open_node_lst.push_back(entity);
		}
	}

	// Iterate over the open nodes
	for (std::vector<Entity*>::iterator i = open_node_lst.begin();
			i != open_node_lst.end(); ++i){
		// Get set of open segments
		Entity * entity = *i;
		stk::mesh::PairIterRelation relations = entity->relations(edgeRank);
		std::vector<Entity*> open_segment_lst;

		for (stk::mesh::PairIterRelation::iterator j = relations.begin();
				j != relations.end(); ++j){
			Entity & source = *j->entity();
			if(entity_open[source.key()]==true){
				open_segment_lst.push_back(&source);
 			}
		}

		// Iterate over the open segments
		for (std::vector<Entity*>::iterator j = open_segment_lst.begin();
				j != open_segment_lst.end(); ++j){
			Entity * segment = *j;
			// Create star of segment
			std::set<EntityKey> subgraph_entity_lst;
			std::set<stkEdge,EdgeLessThan> subgraph_edge_lst;
			topology::star(subgraph_entity_lst,subgraph_edge_lst,*segment);
			// Iterators
			std::set<EntityKey>::iterator firstEntity = subgraph_entity_lst.begin();
			std::set<EntityKey>::iterator lastEntity = subgraph_entity_lst.end();
			std::set<stkEdge>::iterator firstEdge = subgraph_edge_lst.begin();
			std::set<stkEdge>::iterator lastEdge = subgraph_edge_lst.end();

			Subgraph
			subgraph(bulkData_,firstEntity,lastEntity,firstEdge,lastEdge,numDim);

			// Clone open faces
			stk::mesh::PairIterRelation faces = segment->relations(faceRank);
			std::vector<Entity*> open_face_lst;
			// create a list of open faces
			for (stk::mesh::PairIterRelation::iterator k = faces.begin();
					k != faces.end(); ++k){
				Entity & source = *k->entity();
				if(entity_open[source.key()]==true){
					open_face_lst.push_back(&source);
	 			}
			}

			// Iterate over the open faces
			for(std::vector<Entity*>::iterator k = open_face_lst.begin();
					k != open_face_lst.end(); ++k){
				Entity & face = *(*k);
				Vertex faceVertex = subgraph.global_to_local(face.key());
				subgraph.clone_boundary_entity(faceVertex,entity_open);
			}

			// Split the articulation point (current segment)
			Vertex segmentVertex = subgraph.global_to_local(segment->key());
			subgraph.split_articulation_point(segmentVertex,entity_open);
		}
		// All open faces and segments have been dealt with. Split the node articulation point
		// Create star of node
		std::set<EntityKey> subgraph_entity_lst;
		std::set<stkEdge,EdgeLessThan> subgraph_edge_lst;
		topology::star(subgraph_entity_lst,subgraph_edge_lst,*entity);
		// Iterators
		std::set<EntityKey>::iterator firstEntity = subgraph_entity_lst.begin();
		std::set<EntityKey>::iterator lastEntity = subgraph_entity_lst.end();
		std::set<stkEdge>::iterator firstEdge = subgraph_edge_lst.begin();
		std::set<stkEdge>::iterator lastEdge = subgraph_edge_lst.end();
		Subgraph
		subgraph(bulkData_,firstEntity,lastEntity,firstEdge,lastEdge,numDim);

		Vertex node = subgraph.global_to_local(entity->key());
		std::map<Entity*,Entity*> new_connectivity =
				subgraph.split_articulation_point(node,entity_open);


		// Update the connectivity
		for (std::map<Entity*,Entity*>::iterator
				j = new_connectivity.begin();
				j != new_connectivity.end();
				++j){
			Entity* element = (*j).first;
			Entity* newNode = (*j).second;

			int id = static_cast<int>(element->identifier());
			for (int k = 0; k < connectivity_temp.size(); ++k){
				// Need to subtract 1 from element number as stk indexes from 1
				//   and connectivity_temp indexes from 0
				if(connectivity_temp[id-1][k] == entity){
					connectivity_temp[id-1][k] = newNode;
					// Duplicate the parameters of old node to new node
					bulkData_->copy_entity_fields(*entity,*newNode);
				}
			}
		}
	}

	return;
}

/*
 * Default constructor
 */
Subgraph::Subgraph()
{
	return;
}

/*
 * Create a subgraph given two vectors: a vertex list and a edge list.
 *   Subgraph stored as a boost adjacency list.
 *   Maps the subgraph to the global stk mesh graph.
 */
Subgraph::Subgraph(
		stk::mesh::BulkData* bulkData,
		std::set<EntityKey>::iterator firstVertex,
		std::set<EntityKey>::iterator lastVertex,
		std::set<topology::stkEdge>::iterator firstEdge,
		std::set<topology::stkEdge>::iterator lastEdge,
		int numDim)
{
	// stk mesh data
	bulkData_ = bulkData;
	numDim_ = numDim;

	// Insert vertices and create the vertex map
	std::set<EntityKey>::iterator vertexIterator;
	for (vertexIterator = firstVertex;
			vertexIterator != lastVertex;
			++vertexIterator){
		// get global vertex
		EntityKey globalVertex = *vertexIterator;
		// get entity rank
		EntityRank vertexRank = bulkData_->get_entity(globalVertex)->entity_rank();

		// get the new local vertex
		Vertex localVertex = boost::add_vertex(*this);

		localGlobalVertexMap.insert(std::map<Vertex,EntityKey>::value_type(localVertex,globalVertex));
		globalLocalVertexMap.insert(std::map<EntityKey,Vertex>::value_type(globalVertex,localVertex));

		// store entity rank to vertex property
		VertexNamePropertyMap vertexPropertyMap = boost::get(VertexName(),*this);
		boost::put(vertexPropertyMap,localVertex,vertexRank);

	}

	// Add edges to the subgraph
	std::set<topology::stkEdge>::iterator edgeIterator;
	for (edgeIterator = firstEdge;
			edgeIterator != lastEdge;
			++edgeIterator){
		// Get the edge
		topology::stkEdge globalEdge = *edgeIterator;

		// Get global source and target vertices
		EntityKey globalSourceVertex = globalEdge.source;
		EntityKey globalTargetVertex = globalEdge.target;

		// Get local source and target vertices
		Vertex localSourceVertex = globalLocalVertexMap.find(globalSourceVertex)->second;
		Vertex localTargetVertex = globalLocalVertexMap.find(globalTargetVertex)->second;

		Edge localEdge;
		bool inserted;

		EdgeId edge_id = globalEdge.localId;

		boost::tie(localEdge,inserted) = boost::add_edge(localSourceVertex,localTargetVertex,*this);

		assert(inserted);

		// Add edge id to edge property
		EdgeNamePropertyMap edgePropertyMap = boost::get(EdgeName(),*this);
		boost::put(edgePropertyMap,localEdge,edge_id);
	}
	return;
}

/*
 * Return the global entity key given a local subgraph vertex.
 */
EntityKey
Subgraph::local_to_global(Vertex localVertex){

	std::map<Vertex,EntityKey>::const_iterator vertexMapIterator =
			localGlobalVertexMap.find(localVertex);

	assert(vertexMapIterator != localGlobalVertexMap.end());

	return (*vertexMapIterator).second;
}

/*
 * Return local vertex given global entity key.
 */
Vertex
Subgraph::global_to_local(EntityKey globalVertexKey){

	std::map<EntityKey, Vertex>::const_iterator vertexMapIterator =
			globalLocalVertexMap.find(globalVertexKey);

	assert(vertexMapIterator != globalLocalVertexMap.end());

	return (*vertexMapIterator).second;
}

/*
 * Add a vertex in the subgraph.
 *   Mirrors the change in the stk mesh
 *   Input: rank of vertex (entity) to be created
 *   Output: new vertex
 */
Vertex
Subgraph::add_vertex(EntityRank vertex_rank)
{
	// Insert the vertex into the stk mesh
	// First have to request a new entity of rank N
	std::vector<size_t> requests(numDim_+1,0); // number of entity ranks. 1 + number of dimensions
	requests[vertex_rank]=1;
	stk::mesh::EntityVector newEntity;
	bulkData_->generate_new_entities(requests,newEntity);
	Entity & globalVertex = *(newEntity[0]);

	// Insert the vertex into the subgraph
	Vertex localVertex = boost::add_vertex(*this);

	// Update maps
	localGlobalVertexMap.insert(std::map<Vertex,EntityKey>::value_type(localVertex,globalVertex.key()));
	globalLocalVertexMap.insert(std::map<EntityKey,Vertex>::value_type(globalVertex.key(),localVertex));

	// store entity rank to the vertex property
	VertexNamePropertyMap vertexPropertyMap = boost::get(VertexName(),*this);
	boost::put(vertexPropertyMap,localVertex,vertex_rank);

	return localVertex;
}

/*
 * Remove vertex in subgraph
 *   Mirror in stk mesh
 */
void
Subgraph::remove_vertex(Vertex & vertex)
{
	// get the global entity key of vertex
	EntityKey key = local_to_global(vertex);

	// look up entity from key
	Entity* entity = bulkData_->get_entity(key);

	// remove the vertex and key from globalLocalVertexMap and localGlobalVertexMap
	globalLocalVertexMap.erase(key);
	localGlobalVertexMap.erase(vertex);

	// remove vertex from subgraph
	// first have to ensure that there are no edges in or out of the vertex
	boost::clear_vertex(vertex,*this);
	// remove the vertex
	boost::remove_vertex(vertex,*this);

	// destroy all relations to or from the entity
	stk::mesh::PairIterRelation relations = entity->relations();
	for (int i = 0;	i < relations.size(); ++i){
		EdgeId edgeId = relations[i].identifier();

		Entity & target = *(relations[i].entity());

		bulkData_->destroy_relation(*entity,target,edgeId);
	}
	// remove the entity from stk mesh
	bool deleted = bulkData_->destroy_entity(entity);
	assert(deleted);

	return;
}

/*
 * Add edge to local graph.
 *   Mirror change in stk mesh
 *   Return true if edge inserted in local graph, else false.
 *   If false, will not insert edge into stk mesh.
 */
std::pair<Edge,bool>
Subgraph::add_edge(const EdgeId edge_id,
		const Vertex localSourceVertex,
		const Vertex localTargetVertex)
{
	// Add edge to local graph
	std::pair<Edge,bool> localEdge = boost::add_edge(localSourceVertex,localTargetVertex,*this);

	if (localEdge.second == false)
		return localEdge;

	// get global entities
	EntityKey globalSourceKey = local_to_global(localSourceVertex);
	EntityKey globalTargetKey = local_to_global(localTargetVertex);
	Entity* globalSourceVertex = bulkData_->get_entity(globalSourceKey);
	Entity* globalTargetVertex = bulkData_->get_entity(globalTargetKey);

	//testing
	if(globalSourceVertex->entity_rank()-globalTargetVertex->entity_rank() != 1){
		cout << "add edge:" << globalSourceVertex->entity_rank() << "," << globalSourceVertex->identifier() << " "
				<< globalTargetVertex->entity_rank() << "," << globalTargetVertex->identifier() << "\n";
	}

	// Add edge to stk mesh
	bulkData_->declare_relation(*(globalSourceVertex),*(globalTargetVertex),edge_id);

	// Add edge id to edge property
	EdgeNamePropertyMap edgePropertyMap = boost::get(EdgeName(),*this);
	boost::put(edgePropertyMap,localEdge.first,edge_id);

	return localEdge;
}

/*
 * Remove edge from graph
 *   Mirror change in stk mesh
 */
void
Subgraph::remove_edge(
		const Vertex & localSourceVertex,
		const Vertex & localTargetVertex){
	// Get the local id of the edge in the subgraph

	Edge edge;
	bool inserted;
	boost::tie(edge,inserted) =
	boost::edge(localSourceVertex,localTargetVertex,*this);

	assert(inserted);

	EdgeId edge_id = get_edge_id(edge);

	// remove local edge
	boost::remove_edge(localSourceVertex,localTargetVertex,*this);

	// remove relation from stk mesh
	EntityKey globalSourceId = Subgraph::local_to_global(localSourceVertex);
	EntityKey globalTargetId = Subgraph::local_to_global(localTargetVertex);
	Entity* globalSourceVertex = bulkData_->get_entity(globalSourceId);
	Entity* globalTargetVertex = bulkData_->get_entity(globalTargetId);

	bulkData_->destroy_relation(*(globalSourceVertex),*(globalTargetVertex),edge_id);

	return;
}

EntityRank &
Subgraph::get_vertex_rank(const Vertex vertex)
{
	VertexNamePropertyMap vertexPropertyMap = boost::get(VertexName(), *this);

	return boost::get(vertexPropertyMap,vertex);
}

EdgeId &
Subgraph::get_edge_id(const Edge edge)
{
	EdgeNamePropertyMap edgePropertyMap = boost::get(EdgeName(), *this);

	return boost::get(edgePropertyMap,edge);
}

/*
 * The connected components boost algorithm requires an undirected graph.
 *   Subgraph creates a directed graph. Copy all vertices and edges from the
 *   subgraph into the undirected graph with the exception of the input vertex
 *   and all edges in or out of that vertex. Then, check whether input vertex
 *   is an articulation point. Vertex is articulation point if the number of
 *   connected components is greater than 1
 *
 *   Returns the number of connected components and component number of each vertex
 */
void
Subgraph::undirected_graph(Vertex input_vertex,
		int & numComponents,
		std::map<Vertex,int> & subComponent){
	typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS> undirectedGraph;
	typedef boost::graph_traits<undirectedGraph>::vertex_descriptor uVertex;
	typedef boost::graph_traits<undirectedGraph>::edge_descriptor uEdge;
	// Map to and from undirected graph and subgraph
	std::map<uVertex,Vertex> undirectedSubVertexMap;
	std::map<Vertex,uVertex> subUndirectedVertexMap;
	undirectedGraph g;
	VertexIterator vertex_begin;
	VertexIterator vertex_end;
	boost::tie(vertex_begin,vertex_end) = vertices(*this);

	// First add the vertices to the graph
	for (VertexIterator i = vertex_begin; i != vertex_end; ++i){
		Vertex vertex = *i;
		if (vertex != input_vertex){
			uVertex uvertex = boost::add_vertex(g);
			undirectedSubVertexMap[uvertex] = vertex;
			// Add to maps
			undirectedSubVertexMap.insert(std::map<uVertex,Vertex>::value_type(uvertex,vertex));
			subUndirectedVertexMap.insert(std::map<Vertex,uVertex>::value_type(vertex,uvertex));
		}
	}

	// Then add the edges
	for (VertexIterator i = vertex_begin; i != vertex_end; ++i){
		Vertex source = *i;

		if(source != input_vertex){
			std::map<Vertex,uVertex>::const_iterator sourceMapIterator =
					subUndirectedVertexMap.find(source);

			uVertex usource = (*sourceMapIterator).second;

			// write the edges in the subgraph
			out_edge_iterator out_edge_begin;
			out_edge_iterator out_edge_end;
			boost::tie(out_edge_begin,out_edge_end) = out_edges(*i,*this);

			for (out_edge_iterator j = out_edge_begin; j != out_edge_end; ++j){
				Vertex target;
				Edge edge = *j;
				target = boost::target(edge,*this);

				if(target != input_vertex){
					std::map<Vertex,uVertex>::const_iterator targetMapIterator =
							subUndirectedVertexMap.find(target);

					uVertex utarget = (*targetMapIterator).second;

					boost::add_edge(usource,utarget,g);
				}
			}
		}
	}

	std::vector<int> component(boost::num_vertices(g));
	numComponents = boost::connected_components(g, &component[0]);

	for (std::map<uVertex,Vertex>::iterator i = undirectedSubVertexMap.begin();
			i != undirectedSubVertexMap.end(); ++i){
		Vertex vertex = (*i).second;
		subComponent.insert(std::map<Vertex,int>::value_type(vertex,component[(*i).first]));
	}


	return;
}

/*
 * Clones a boundary entity from the subgraph and separates the in-edges
 *   of the entity.
 *   Boundary entities are on boundary of the elements in the mesh. They
 *   will thus have either 1 or 2 in-edges to elements.
 *
 *   If there is only 1 in-edge, the entity is an exterior entity of the
 *   mesh and is not a candidate for fracture. If only 1 in-edge: Return error.
 *
 *   Entity must have satisfied the fracture criterion and be labeled open
 *   in is_open. If not open: Return error.
 */
void
Subgraph::clone_boundary_entity(Vertex & vertex,std::map<EntityKey,bool> & entity_open){
	// Check that number of in_edges = 2
	boost::graph_traits<boostGraph>::degree_size_type num_in_edges = boost::in_degree(vertex,*this);
	if (num_in_edges!=2)
		return;

	// Check that vertex = open
	EntityKey vertKey = Subgraph::local_to_global(vertex);
	assert(entity_open[vertKey]==true);

	// Get the vertex rank
	EntityRank vertexRank = Subgraph::get_vertex_rank(vertex);

	// Create a new vertex of same rank as vertex
	Vertex newVertex = Subgraph::add_vertex(vertexRank);

	// Copy the out_edges of vertex to newVertex
	out_edge_iterator out_edge_begin;
	out_edge_iterator out_edge_end;
	boost::tie(out_edge_begin,out_edge_end) = boost::out_edges(vertex,*this);
	for (out_edge_iterator i = out_edge_begin; i != out_edge_end; ++i){
		Edge edge = *i;
		EdgeId edgeId = Subgraph::get_edge_id(edge);
		Vertex target = boost::target(edge,*this);
		Subgraph::add_edge(edgeId,newVertex,target);
	}

	// Copy all out edges not in the subgraph to the new vertex
	Subgraph::clone_out_edges(vertex,newVertex);

	// Remove one of the edges from vertex, copy to newVertex
	// Arbitrarily remove the first edge from original vertex
	in_edge_iterator in_edge_begin;
	in_edge_iterator in_edge_end;
	boost::tie(in_edge_begin,in_edge_end) = boost::in_edges(vertex,*this);
	Edge edge = *(in_edge_begin);
	EdgeId edgeId = Subgraph::get_edge_id(edge);
	Vertex source = boost::source(edge,*this);
	Subgraph::remove_edge(source,vertex);

	// Add edge to new vertex
	Subgraph::add_edge(edgeId,source,newVertex);

	// Have to clone the out edges of the original entity to the new entity.
	// These edges are not in the subgraph

	// Clone process complete, set entity_open to false
	entity_open[vertKey] = false;

	return;
}

/*
 * Splits an articulation point.
 *   An articulation point is defined as a vertex which if removed
 *   yields a graph with more than 1 connected components. Creates
 *   an undirected graph and checks connected components of graph without
 *   vertex. Check if vertex is articulation point
 *
 *   Clones articulation point and splits in-edges between original and new
 *   vertices.
 */
std::map<Entity*,Entity* >
Subgraph::split_articulation_point(Vertex vertex,std::map<EntityKey,bool> & entity_open){
	// Check that vertex = open
	EntityKey vertKey = Subgraph::local_to_global(vertex);
	assert(entity_open[vertKey]==true);

	// get rank of vertex
	EntityRank vertexRank = Subgraph::get_vertex_rank(vertex);

	// Create undirected graph
	int numComponents;
	std::map<Vertex,int> components;
	Subgraph::undirected_graph(vertex,numComponents,components);

	// The function returns an updated connectivity map. If the vertex rank is
	//   not node, then this map will be of size 0.
	std::map<Entity*,Entity* > new_connectivity;

	// Check number of connected components in undirected graph. If = 1, return
	if (numComponents == 1)
		return new_connectivity;

	// If number of connected components > 1, split vertex in subgraph and stk mesh
	// number of new vertices = numComponents - 1
	std::vector<Vertex> newVertex;
	for (int i = 0; i < numComponents-1; ++i){
		Vertex newVert = Subgraph::add_vertex(vertexRank);
		newVertex.push_back(newVert);
	}

	// create a map of elements to new node numbers
	// only do this if the input vertex is a node (don't require otherwise)
	if(vertexRank==0){
		for(std::map<Vertex,int>::iterator i = components.begin();
				i != components.end();
				++i){
			int componentNum = (*i).second;
			Vertex currentVertex = (*i).first;
			EntityRank currentRank = Subgraph::get_vertex_rank(currentVertex);
			// Only add to map if the vertex is an element
			if(currentRank == numDim_ && componentNum != 0){
				Entity* element =
						bulkData_->get_entity(Subgraph::local_to_global(currentVertex));
				Entity* newNode =
						bulkData_->get_entity(Subgraph::local_to_global(newVertex[componentNum-1]));
				new_connectivity.insert(std::map<Entity*,Entity*>::value_type(element,newNode));
			}
		}
	}

	// Copy the out edges of the original vertex to the new vertex
	for (int i = 0; i < newVertex.size(); ++i){
		Subgraph::clone_out_edges(vertex,newVertex[i]);
	}

	// vector for edges to be removed. Vertex is source and edgeId the local id of the edge
	std::vector<std::pair<Vertex,EdgeId> > removed;

	// Iterate over the in edges of the vertex to determine which will be removed
	in_edge_iterator in_edge_begin;
	in_edge_iterator in_edge_end;
	boost::tie(in_edge_begin,in_edge_end) = boost::in_edges(vertex,*this);
	for (in_edge_iterator i = in_edge_begin; i != in_edge_end; ++i){
		Edge edge = *i;
		Vertex source = boost::source(edge,*this);

		std::map<Vertex, int>::const_iterator componentIterator =
					components.find(source);
		int vertComponent = (*componentIterator).second;
		Entity& entity =
				*(bulkData_->get_entity(Subgraph::local_to_global(source)));
		// Only replace edge if vertex not in component 0
		if (vertComponent != 0){
			EdgeId edgeId = Subgraph::get_edge_id(edge);
			removed.push_back(std::make_pair(source,edgeId));
		}
	}

	// remove all edges in vector removed and replace with new edges
	for (std::vector<std::pair<Vertex,EdgeId> >::iterator
			i = removed.begin();
			i != removed.end();
			++i){
		std::pair<Vertex,EdgeId> edge = *i;
		Vertex source = edge.first;
		EdgeId edgeId = edge.second;
		std::map<Vertex, int>::const_iterator componentIterator =
					components.find(source);
		int vertComponent = (*componentIterator).second;

		Subgraph::remove_edge(source,vertex);
		std::pair<Edge,bool> inserted =
				Subgraph::add_edge(edgeId,source,newVertex[vertComponent-1]);
		assert(inserted.second==true);
	}

	// split process complete, set entity_open to false
	entity_open[vertKey] = false;

	return new_connectivity;
}

/*
 * Given an original and new vertex in the subgraph clones the out edges of
 *   the original vertex to the new vertex. If the out edge is already in
 *   out edges of the vertex (the edge was added in a previous step), does
 *   not add a new edge. The edges added are added to the subgraph, are
 *   only represented in the global stk mesh
 */
void
Subgraph::clone_out_edges(Vertex & originalVertex, Vertex & newVertex){
	// Get the entity for the original and new vertices
	EntityKey originalKey = Subgraph::local_to_global(originalVertex);
	EntityKey newKey = Subgraph::local_to_global(newVertex);
	Entity & originalEntity = *(bulkData_->get_entity(originalKey));
	Entity & newEntity = *(bulkData_->get_entity(newKey));

	// Iterate over the out edges of the original vertex and check against the
	//   out edges of the new vertex. If the edge does not exist, add.
	stk::mesh::PairIterRelation originalRelations =
			originalEntity.relations(originalEntity.entity_rank()-1);
	for (int i = 0; i < originalRelations.size(); ++i){
		stk::mesh::PairIterRelation newRelations =
				newEntity.relations(newEntity.entity_rank()-1);
		// assume the edge doesn't exist
		bool exists = false;
		for (int j = 0; j < newRelations.size(); ++j){
			if(originalRelations[i].entity() == newRelations[j].entity()){
				exists = true;
			}
		}
		if(exists == false){
			EdgeId edgeId = originalRelations[i].identifier();
			Entity& target = *(originalRelations[i].entity());
			bulkData_->declare_relation(newEntity,target,edgeId);
		}
	}

	return;
}

/*
 * Similar to the function in the topology class. Will output the subgraph in a
 * .dot file readable by the graphviz program dot.
 *
 * To create a figure from the file:
 *   dot -Tpng output.dot -o output.png
 */
void
Subgraph::output_to_graphviz(
		std::string & gviz_output,
		std::map<EntityKey,bool> entity_open)
{
	// Open output file
	std::ofstream gviz_out;
	gviz_out.open (gviz_output.c_str(), std::ios::out);

	cout << "Write graph to graphviz dot file\n";

	if (gviz_out.is_open()){
		// Write beginning of file
		gviz_out << "digraph mesh {\n"
				 << "  node [colorscheme=paired12]\n"
				 << "  edge [colorscheme=paired12]\n";

		VertexIterator vertex_begin;
		VertexIterator vertex_end;
		boost::tie(vertex_begin,vertex_end) = vertices(*this);

		for (VertexIterator i = vertex_begin; i != vertex_end; ++i){
			EntityKey key = local_to_global(*i);
			Entity & entity = *(bulkData_->get_entity(key));
			std::string label;
			std::string color;

			// Write the entity name
			switch(entity.entity_rank()){
			// nodes
			case 0:
				label = "Node";
				if (entity_open[entity.key()]==false)
					color = "6";
				else
					color = "5";
				break;
			// segments
			case 1:
				label = "Segment";
				if (entity_open[entity.key()]==false)
					color = "4";
				else
					color = "3";
				break;
			// faces
			case 2:
				label = "Face";
				if (entity_open[entity.key()]==false)
					color = "2";
				else
					color = "1";
				break;
			// volumes
			case 3:
				label = "Element";
				if (entity_open[entity.key()]==false)
					color = "8";
				else
					color = "7";
				break;
			}
			gviz_out << "  \"" << entity.identifier() << "_" << entity.entity_rank()
				<< "\" [label=\" " << label << " " << entity.identifier()
				<< "\",style=filled,fillcolor=\"" << color << "\"]\n";

			// write the edges in the subgraph
			out_edge_iterator out_edge_begin;
			out_edge_iterator out_edge_end;
			boost::tie(out_edge_begin,out_edge_end) = out_edges(*i,*this);

			for (out_edge_iterator j = out_edge_begin; j != out_edge_end; ++j){
				Edge out_edge = *j;
				Vertex source = boost::source(out_edge,*this);
				Vertex target = boost::target(out_edge,*this);

				EntityKey sourceKey = local_to_global(source);
				Entity & global_source = *(bulkData_->get_entity(sourceKey));

				EntityKey targetKey = local_to_global(target);
				Entity & global_target = *(bulkData_->get_entity(targetKey));

				EdgeId edgeId = get_edge_id(out_edge);

				switch(edgeId){
				case 0:
					color = "6";
					break;
				case 1:
					color = "4";
					break;
				case 2:
					color = "2";
					break;
				case 3:
					color = "8";
					break;
				case 4:
					color = "10";
					break;
				case 5:
					color = "12";
					break;
				default:
					color = "9";
				}
				gviz_out << "  \"" << global_source.identifier() << "_" << global_source.entity_rank()
						 << "\" -> \"" << global_target.identifier() << "_" << global_target.entity_rank()
						 << "\" [color=\"" << color << "\"]" << "\n";
			}

		}

		// File end
		gviz_out << "}";
		gviz_out.close();
	}
	else
		cout << "Unable to open graphviz output file 'output.dot'\n";

	return;
}

} // namespace LCM

